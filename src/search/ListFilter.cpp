#include "search/SearchInternal.h"
#include "sorting/Picker.h"

#include "search/SearchConfig.h"
#include "engine/ContainerMenu.h"
#include "engine/PEHook.h"
#include "engine/QuestsMenu.h"
#include "engine/Reflect.h"
#include "engine/TextUtil.h"
#include "engine/Ui.h"
#include "engine/UiText.h"

namespace msearch
{
	namespace
	{
		using namespace detail;

		int ItemCount(UE::UObject* a_list)
		{
			if (!a_list) return -1;
			ui::Call n(a_list, L"GetNumItems");
			if (n && n.Run()) return n.Get<std::int32_t>("ReturnValue");
			ui::Call g(a_list, L"GetListItems");
			void*    ret = g ? g.At("ReturnValue") : nullptr;
			if (!ret || !g.Run()) return -1;
			const int num = *reinterpret_cast<std::int32_t*>(static_cast<std::uint8_t*>(ret) + 8);   // TArray: data pointer, then the count
			if (void* data = *reinterpret_cast<void**>(ret)) UE::FMemory::Free(data);   // the engine allocated the copy
			return num;
		}

		UE::UObject* ListViewOf(UE::UObject* a_widget)
		{
			if (ItemCount(a_widget) >= 0) return a_widget;
			for (const wchar_t* fn : { L"GetListView", L"GetItemListView" }) {
				ui::Call g(a_widget, fn);
				if (g && g.Run()) {
					auto* inner = g.Get<UE::UObject*>("ReturnValue");
					if (inner && ItemCount(inner) >= 0) return inner;
				}
			}
			return nullptr;
		}

		// ---- reading the lists ----
		// The items of a list view (GetListItems copies the array: the copy's memory is ours to free)
		std::vector<UE::UObject*> ListItemsOf(UE::UObject* a_list)
		{
			std::vector<UE::UObject*> out;
			ui::Call                 g(a_list, L"GetListItems");
			void*                    ret = g ? g.At("ReturnValue") : nullptr;
			if (!ret || !g.Run()) return out;
			auto*      data = *reinterpret_cast<UE::UObject***>(ret);
			const auto num = *reinterpret_cast<std::int32_t*>(static_cast<std::uint8_t*>(ret) + 8);
			for (std::int32_t i = 0; data && i < num && i < 4000; ++i) {
				if (data[i]) out.push_back(data[i]);
			}
			if (data) UE::FMemory::Free(data);
			return out;
		}

		struct FilterState
		{
			reflect::Handle list, owner, cache;
			Clock::time_point nextLook{};
			revamped::ListLifecycle<UE::UObject*> lifecycle;
			std::vector<reflect::Handle> roots;
			bool watching = false;
			void Reset()
			{
				if (auto* old = reflect::Get(cache)) { ui::Call remove(old, L"RemoveFromParent"); remove.Run(); }
				const auto generation = lifecycle.generation + 1;
				*this = {};
				lifecycle.generation = generation;
			}
		};
		bool g_filtering = false;

		FilterState g_filter[6];   // by menu
	}

	using namespace detail;

	// ---- the filter ----
	// The search text is that of the search bar. A copy of the full list - the items as the game built them -
	// is kept while nothing is searched; with a search active the list is rewritten to the matching items (a case-insensitive part of the
	// item's name) by ClearListItems / AddItem. When the game rebuilds the whole list (after a press) it is filtered again on the next read
	// of the controller, a frame at most - the search mods take a worker-thread wake or two (up to ~130 ms) for the same.
	static void ApplyItems(UE::UObject* a_list, const std::vector<UE::UObject*>& a_items)
	{
		ui::Call clear(a_list, L"ClearListItems");
		clear.Run();
		for (auto* it : a_items) {
			ui::CallFirst(a_list, L"AddItem", &it, sizeof(it));
		}
		ui::Call regen(a_list, L"RegenerateAllEntries");
		regen.Run();
		ui::Call refresh(a_list, L"RequestRefresh");
		refresh.Run();
	}

	static void RegisterMenuHooks();

	static void OnListPost(UE::UObject* object, UE::UFunction* fn, void*)
	{
		if (g_filtering) return;
		const auto name = pe::FunctionName(fn);
		if (name != "ClearListItems" && name != "AddItem" && name != "SetListItems" && name != "RemoveItem") return;
		for (auto& state : g_filter)
			if (reflect::Get(state.list) == object) state.lifecycle.nativeDirty = true;
	}

	void FilterTick(menus::Menu menu, Clock::time_point now)
	{
		if (g_filtering || !reflect::Ok()) return;
		g_filtering = true;
		struct Exit { ~Exit() { g_filtering = false; } } exit;
		RegisterMenuHooks();
		const auto context = menus::Active();
		for (int i = 1; i <= 5; ++i)
			if (!Present(static_cast<menus::Menu>(i), context) && g_filter[i].watching) g_filter[i].Reset();
		const int id = static_cast<int>(menu);
		if (id < 1 || id > 5 || !Present(menu, context)) return;
		if (IsSide(menu) && !SideShownNow(menu)) return;
		auto& state = g_filter[id];
		auto* owner = IsSide(menu) ? container::SideContent(menu) :
			menu == menus::Menu::kQuests ? quests::ListWrapper() : menus::Widget(menu);
		if (!owner) return;
		if (state.watching && reflect::Get(state.owner) != owner) state.Reset();
		if (!reflect::Get(state.list)) {
			if (now < state.nextLook) return;
			state.nextLook = now + 500ms;
			auto* wrapper = menu == menus::Menu::kQuests ? quests::ListWrapper() :
				ui::FindInTree(owner, menu == menus::Menu::kMagic ? "Magic_Spell_List" : "inv_cont_listview");
			auto* view = wrapper ? ListViewOf(wrapper) : nullptr;
			auto* canvas = BarCanvasFor(menu, false);
			if (!view || !canvas) return;
			UE::UObject* slot = nullptr;
			auto* cache = ui::AddToCanvas(canvas, L"/Script/UMG.ListView",
				(L"MrSourceCache" + std::to_wstring(id)).c_str(), &slot);
			if (!cache) return; // Never filter without rooting the complete native source.
			ui::Visible(cache, false);
			state.cache = reflect::Hold(cache);
			state.list = reflect::Hold(view); state.owner = reflect::Hold(owner);
			state.watching = true;
			pe::WatchPost(view->GetClass(), &OnListPost);
		}
		auto* list = reflect::Get(state.list);
		if (!list || ItemCount(list) < 0 || ItemCount(list) > 4000) return;
		const auto live = ListItemsOf(list);
		if (state.lifecycle.Observe(live)) {
			state.roots.clear();
			for (auto* item : live) state.roots.push_back(reflect::Hold(item));
			auto* cache = reflect::Get(state.cache);
			if (!cache) { state.Reset(); return; }
			ui::Call clear(cache, L"ClearListItems"); clear.Run();
			for (auto* item : live) ui::CallFirst(cache, L"AddItem", &item, sizeof(item));
		}
		std::vector<UE::UObject*> source;
		for (const auto& root : state.roots) {
			auto* item = reflect::Get(root);
			if (!item) { state.Reset(); return; }
			source.push_back(item);
		}
		const auto query = searchcfg::Get().ownFilter && MenuEnabled(menu) ? Lower(QueryOf(menu)) : std::string();
		const auto& categories = id <= 4 ? sorting::Evaluate(id, source, state.lifecycle.generation) : source;
		std::vector<UE::UObject*> keep;
		bool readable = query.empty() || menu != menus::Menu::kQuests;
		for (auto* item : categories) {
			if (menu == menus::Menu::kQuests && !ItemName(item, menu).empty()) readable = true;
			if (query.empty() || Matches(item, menu, query)) keep.push_back(item);
		}
		if (!readable) keep = source; // Unreadable quest names fail open.
		if (keep != live) ApplyItems(list, keep);
		state.lifecycle.Commit(keep);
	}

	static void FilterQuick(menus::Menu menu, const char* after)
	{
		const int id = static_cast<int>(menu);
		if (id < 1 || id > 5 || g_filtering) return;
		// Native callbacks, not pointer equality, establish a rebuilt generation.
		if (std::string_view(after) == "UpdateItemList") g_filter[id].lifecycle.nativeDirty = true;
		FilterTick(menu, Clock::now());
	}

	static void OnMenuPost(UE::UObject* a_obj, UE::UFunction* a_fn, void*)
	{
		static bool s_in = false;   // our own calls into the list never come back here, but be sure
		if (s_in || g_filtering || !a_obj) return;
		auto* cls = a_obj->GetClass();
		if (cls && (cls == quests::PageClass() || cls == quests::PanelClass())) {   // the quests tab
					if (cls == quests::PageClass()) quests::NoteInstance(a_obj);
s_in = true;
					FilterQuick(menus::Menu::kQuests, pe::FunctionName(a_fn).c_str());
					s_in = false;
					return;
				}
				if (cls && cls == container::Class()) {   // the container / barter menu: both sides
			s_in = true;
			FilterQuick(menus::Menu::kContainerYours, pe::FunctionName(a_fn).c_str());
			FilterQuick(menus::Menu::kContainerTheirs, pe::FunctionName(a_fn).c_str());
			s_in = false;
			return;
		}
		const auto menu = cls == menus::ClassOf(menus::Menu::kMagic) ? menus::Menu::kMagic : cls == menus::ClassOf(menus::Menu::kInventory) ? menus::Menu::kInventory : menus::Menu::kNone;
		if (menu == menus::Menu::kNone) return;
		s_in = true;
		FilterQuick(menu, pe::FunctionName(a_fn).c_str());
		s_in = false;
	}

	static void RegisterMenuHooks()
	{
		static bool s_done[3] = { false, false, false };
		for (const auto m : { menus::Menu::kInventory, menus::Menu::kMagic }) {
			const int i = static_cast<int>(m);
			if (s_done[i]) continue;
			if (auto* cls = menus::ClassOf(m); cls && pe::WatchPost(cls, &OnMenuPost)) {
				s_done[i] = true;
				logger::debug("own filter: watching the {}'s calls to filter right after them", menus::Name(m));
			}
		}
		static bool s_doneQuestPage = false, s_doneQuestPanel = false;
				if (!s_doneQuestPage) {
					if (auto* cls = quests::PageClass(); cls && pe::WatchPost(cls, &OnMenuPost)) {
						s_doneQuestPage = true;
						logger::debug("own filter: watching the quests page's calls to filter right after them");
					}
				}
				if (!s_doneQuestPanel) {
					if (auto* cls = quests::PanelClass(); cls && pe::WatchPost(cls, &OnMenuPost)) {
						s_doneQuestPanel = true;
						logger::debug("own filter: watching the quests list panel's calls to filter right after them");
					}
				}
				static bool s_doneContainer = false;
		if (!s_doneContainer) {
			if (auto* cls = container::Class(); cls && pe::WatchPost(cls, &OnMenuPost)) {
				s_doneContainer = true;
				logger::debug("own filter: watching the container / barter menu's calls to filter right after them");
			}
		}
	}
}
