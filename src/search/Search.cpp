#include "search/Search.h"
#include "sorting/Picker.h"
#include "search/ListLifecycle.h"

#include "search/SearchConfig.h"
#include "engine/ContainerMenu.h"
#include "engine/PEHook.h"
#include "engine/QuestsMenu.h"
#include "engine/Reflect.h"
#include "engine/TextUtil.h"
#include "engine/Ui.h"
#include "engine/UiText.h"

#include <imm.h>

#include <cmath>
#include <cstring>
#include <unordered_set>

namespace msearch
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr std::size_t kMaxText = 40;    // characters (code points) of a search text
		constexpr std::size_t kMaxBytes = 120;  // and bytes of its UTF-8: the controller keyboard reads it through a 128-byte buffer
		wchar_t               g_highSurrogate = 0;   // the first half of a typed character outside the BMP, until the second half arrives - under g_qMutex

		// the text of the search bar (one per menu, UTF-8), and the flags the window procedure shares with the game thread
		std::mutex           g_qMutex;             // the window procedure may run on another thread than the game's
		std::string          g_queries[6];
				bool                 g_selAll[6] = {};   // Ctrl+A: the whole text is selected (the next key replaces it, Backspace / Delete clears it) - under g_qMutex   // by menu: kInventory, kMagic, kContainerYours, kContainerTheirs
		std::atomic<bool>    g_typing{ false };
		std::atomic<int>     g_typingMenu{ 0 };
		std::atomic<bool>    g_sideShown[5];   // the container / barter menu's sides that are on the screen now (a plain container shows only its own)
					double               g_canvasW = 1920;   // the container menu's root canvas, menu units (for which half of the screen a list is on)
					std::atomic<bool>    g_external{ false };   // the controller keyboard (another plugin) has the text: typed keys are not taken
		DWORD                g_gameThread = 0;

		bool IsSide(menus::Menu a_menu) { return a_menu == menus::Menu::kContainerYours || a_menu == menus::Menu::kContainerTheirs; }
		bool MenuEnabled(menus::Menu a_menu)
		{

			return (a_menu == menus::Menu::kInventory && searchcfg::Get().inventory) || (a_menu == menus::Menu::kMagic && searchcfg::Get().magic)|| (IsSide(a_menu) && searchcfg::Get().container && g_sideShown[static_cast<int>(a_menu)].load()) || (a_menu == menus::Menu::kQuests && searchcfg::Get().quests);
		}
		bool OwnBarOn() { return (searchcfg::Get().inventory || searchcfg::Get().magic || searchcfg::Get().container || searchcfg::Get().quests); }
		// a menu is present when it is the open one; in the container / barter menu both sides are
		bool Present(menus::Menu a_menu, menus::Menu a_active)
		{
			return a_menu == a_active || (IsSide(a_menu) && a_active == menus::Menu::kNone && container::Open()) || (a_menu == menus::Menu::kQuests && a_active == menus::Menu::kNone && quests::Open());
		}
		UE::UObject* MenuWidget(menus::Menu a_menu) { return IsSide(a_menu) ? container::Widget() : a_menu == menus::Menu::kQuests ? quests::Page() : menus::Widget(a_menu); }
		std::string QueryOf(menus::Menu a_menu)
		{
			std::scoped_lock l(g_qMutex);
			return g_queries[static_cast<int>(a_menu) % 6];
		}
		bool SelAllOf(menus::Menu a_menu)
				{
					std::scoped_lock l(g_qMutex);
					return g_selAll[static_cast<int>(a_menu) % 6];
				}
				void SetQuery(menus::Menu a_menu, const std::string& a_text)
{
			std::scoped_lock l(g_qMutex);
			g_queries[static_cast<int>(a_menu) % 6] = a_text;
		}

		// the user widget (the menu) a widget sits in
		UE::UObject* OwnerWidget(UE::UObject* a_o)
		{
			static auto* userWidget = ui::Class(L"/Script/UMG.UserWidget");
			for (auto* p = a_o ? a_o->GetOuter() : nullptr; p && userWidget; p = p->GetOuter()) {
				if (p->GetClass() && p->GetClass()->IsChildOf(userWidget)) return p;
			}
			return nullptr;
		}


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

		// a list item's name: an FText-returning accessor if it has one, else the Properties struct's Name (as the row widgets carry it)
		// a name for a row of a kind we do not know the properties of (the quests tab): a name accessor if it has one, else the item's own text properties
		// (the ones called ...Name... or ...Title... first); the class and its text properties are logged once
		std::string GenericItemName(UE::UObject* a_item, const char** a_route)
		{
			static bool s_logged = false;
			std::vector<std::pair<std::string, std::string>> texts;   // property name, its text
			{   // the row's GetProperties() struct (LegacyMapMenuQuestProperties): its text properties
				static auto* props = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Altar.LegacyMapMenuQuestProperties"));
				static bool  s_said = false;
				ui::Call     g(a_item, L"GetProperties");
				void*        ret = g && props ? g.At("ReturnValue") : nullptr;
				if (!s_said) {
					s_said = true;
					logger::debug("own filter (quests): GetProperties {}, the struct {}, its size {} / the call's {}", g ? "exists" : "is missing", props ? "is there" : "is missing", props ? props->propertiesSize : -1, g && props ? g.Size("ReturnValue") : -1);
					if (props) {
						for (const auto& [name, off] : reflect::Fields(props)) logger::debug("own filter (quests):   {}.{} @0x{:X} size {}", "LegacyMapMenuQuestProperties", name, off, ui::SizeOf(props, name));
					}
				}
				if (ret && g.Size("ReturnValue") == props->propertiesSize && g.Run()) {
					for (const auto& [name, off] : reflect::Fields(props)) {
						const auto size = ui::SizeOf(props, name);
						if (off < 0 || size != static_cast<std::int32_t>(sizeof(UE::FText)) || off + size > props->propertiesSize) continue;
						auto* t = reinterpret_cast<UE::FText*>(static_cast<std::uint8_t*>(ret) + off);
						if (reflect::TextSet(*t)) texts.emplace_back(name, reflect::Text(*t));
						t->~FText();
					}
				}
			}
for (UE::UStruct* c = a_item->GetClass(); c; c = c->GetSuperStruct()) {
				if (pe::Utf8(c->GetFName().ToString()) == "Object") break;
				for (const auto& [name, off] : reflect::Fields(c)) {
					const auto size = ui::SizeOf(c, name);
					if (off < 0 || size != static_cast<std::int32_t>(sizeof(UE::FText)) || off + size > c->propertiesSize) continue;
					const auto& t = *reinterpret_cast<const UE::FText*>(reinterpret_cast<const std::uint8_t*>(a_item) + off);
					if (!reflect::TextSet(t)) continue;
					texts.emplace_back(name, reflect::Text(t));
				}
			}
			if (!s_logged) {
				s_logged = true;
				logger::debug("own filter (quests): a list item is {}; its text properties: {}", pe::Utf8(a_item->GetClass()->GetFName().ToString()), texts.empty() ? std::string("none") : std::string());
				for (const auto& [n, t] : texts) logger::debug("own filter (quests):   {} = {}", n, t.substr(0, 60));
			}
			for (const char* want : { "name", "title" }) {
				for (const auto& [n, t] : texts) {
					std::string low = n;
					for (auto& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
					if (!t.empty() && low.find(want) != std::string::npos) {
						if (a_route) *a_route = "text property";
						return t;
					}
				}
			}
			std::string all;
			for (const auto& [n, t] : texts) all += t + " ";
			if (!all.empty() && a_route) *a_route = "text properties";
			return all;
		}

		std::string ItemName(UE::UObject* a_item, menus::Menu a_menu, const char** a_route = nullptr)
{
			static auto* magicProps = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Altar.LegacyMagicMenuItemProperties"));
			static auto* invProps = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Altar.OriginalInventoryMenuItemProperties"));
			if (!a_item || !a_item->GetClass() || !reflect::Ok() || !reflect::IsLive(a_item)) return {};
			for (const wchar_t* fn : { L"GetNameText", L"GetDisplayNameText" }) {
				ui::Call c(a_item, fn);
				void*    ret = c ? c.At("ReturnValue") : nullptr;
				if (!ret || c.Size("ReturnValue") != static_cast<std::int32_t>(sizeof(UE::FText)) || !c.Run()) continue;
				auto*       t = static_cast<UE::FText*>(ret);
				std::string s = reflect::Text(*t);
				t->~FText();
				if (!s.empty()) {
					if (a_route) *a_route = fn[3] == L'N' ? "GetNameText" : "GetDisplayNameText";
					return s;
				}
			}
			if (a_menu == menus::Menu::kQuests) return GenericItemName(a_item, a_route);
						auto* props = a_menu == menus::Menu::kMagic ? magicProps : invProps;
			ui::Call g(a_item, L"GetProperties");
			void*    ret = g && props ? g.At("ReturnValue") : nullptr;
			const auto nameOff = props ? reflect::Offset(props, "Name") : -1;
			if (!ret || nameOff < 0 || g.Size("ReturnValue") != props->propertiesSize || !g.Run()) return {};
			auto*       t = reinterpret_cast<UE::FText*>(static_cast<std::uint8_t*>(ret) + nameOff);
			std::string s = reflect::Text(*t);
			t->~FText();
			if (a_route && !s.empty()) *a_route = "GetProperties";
			return s;
		}

		std::string Lower(const std::string& a_s)
		{
			return textutil::Lower(a_s);
		}

		// does an item's name hold the search text (a_query is Lower() of what was typed)?
		bool Matches(UE::UObject* a_item, menus::Menu a_menu, const std::string& a_query)
		{
			return textutil::Contains(ItemName(a_item, a_menu), a_query, searchcfg::Get().ignoreAccents);
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

		// ---- our own search bar (Inventory and Magic) ----
		// A bar in the menu's own root canvas, where the search mods put theirs, and the typing for it: while it is being typed into, the
		// game window's keys are taken here (the menu's hotkeys never see them), Ctrl+F / Ctrl+K or a click on the bar starts typing, Enter or
		// Esc ends it. The text lives in g_queries; the filter and the controller's keyboard read and write the same text.
		constexpr float kBarFill[4] = { 0.16f, 0.09f, 0.04f, 1.0f }, kBarEdge[4] = { 0.06f, 0.035f, 0.018f, 1.0f }, kBarFocus[4] = { 0.78f, 0.62f, 0.28f, 1.0f };
		constexpr float kBarInk[4] = { 0.93f, 0.90f, 0.82f, 1.0f }, kBarHint[4] = { 0.58f, 0.53f, 0.45f, 1.0f };

		struct BarWidgets
		{
			reflect::Handle   canvas, frame, edge, label, frameSlot, labelSlot;
			bool              textured = false;
			std::string       shown;
			std::string       hint = "Search";   // what the empty bar says (the container / barter menu's say whose list it is)
			bool              typing = false;
			bool              visible = true;
			double            x = 0, y = 0, w = 0, h = 0;   // where it sits in its canvas (menu units)
			Clock::time_point nextLook{}, nextMeasure{};
		};
		BarWidgets g_bar[6];
		struct PxRect { double x = 0, y = 0, w = 0, h = 0; bool valid = false; };
		PxRect  g_barPx[6];   // the bar's place in window pixels (g_qMutex)
		HWND    g_hwnd = nullptr;


		float ViewportScale()
		{
			static auto* lib = ui::Class(L"/Script/UMG.WidgetLayoutLibrary");
			auto*        cdo = lib ? lib->GetDefaultObject(false) : nullptr;
			auto*        pc = ui::PlayerController();
			if (!cdo || !pc) return 1.0f;
			ui::Call c(cdo, L"GetViewportScale");
			if (!c) return 1.0f;
			c.Set("WorldContextObject", pc);
			return c.RunGuarded() ? c.Get<float>("ReturnValue") : 1.0f;
		}

		// the root canvas the bar goes into: that of the user widget that holds the list (the Inventory's search bar is in a widget inside the
		// menu), else the menu's own
		UE::UObject* BarCanvasFor(menus::Menu a_menu, bool a_log = true)
		{
			static auto* canvasClass = ui::Class(L"/Script/UMG.CanvasPanel");
			static auto* treeClass = ui::Class(L"/Script/UMG.WidgetTree");
			auto*        menuW = MenuWidget(a_menu);
							if (!menuW || !canvasClass || !treeClass || !reflect::Ok()) return nullptr;
			auto* wrapper = IsSide(a_menu) || a_menu == menus::Menu::kQuests ? nullptr : ui::FindInTree(menuW, a_menu == menus::Menu::kMagic ? "Magic_Spell_List" : "inv_cont_listview");
			auto* owner = wrapper ? OwnerWidget(wrapper) : nullptr;
			std::vector<UE::UObject*> cands{ menuW, owner };
						if (a_menu == menus::Menu::kQuests) {
							for (auto* p = OwnerWidget(menuW); p; p = OwnerWidget(p)) cands.push_back(p);
						}
						for (auto* w : cands) {
				if (!w || !w->GetClass()) continue;
				const auto wsOff = reflect::Offset(w->GetClass(), "WidgetTree");
				auto**      tree = wsOff >= 0 ? reflect::At<UE::UObject*>(w, wsOff) : nullptr;
				if (!tree || !*tree) continue;
				const auto rootOff = reflect::Offset(treeClass, "RootWidget");
				auto**      root = rootOff >= 0 ? reflect::At<UE::UObject*>(*tree, rootOff) : nullptr;
				if (root && *root && (*root)->GetClass() && (*root)->GetClass()->IsChildOf(canvasClass)) {
					if (a_log) logger::debug("own bar: the {}'s bar goes into the root canvas of {}", menus::Name(a_menu), pe::Utf8(w->GetClass()->GetFName().ToString()));
					return *root;
				}
			}
			return nullptr;
		}

		// the bar's panel: a PNG from the plugin's folder (black for the Inventory and the Magic menu, green for your side of the container / barter menu, red
		// for the other side), else the black one, else a plain rounded box
		UE::UObject* BarTexture(menus::Menu a_menu)
		{
			const int slot = a_menu == menus::Menu::kContainerYours ? 1 : a_menu == menus::Menu::kContainerTheirs ? 2 : 0;
			static reflect::Handle s_tex[3];
			static bool            s_failed[3] = { false, false, false };
			if (auto* t = reflect::Get(s_tex[slot])) return t;
			if (s_failed[slot]) return slot ? BarTexture(menus::Menu::kInventory) : nullptr;
			const wchar_t* file = slot == 1 ? L"SearchBarYours.png" : slot == 2 ? L"SearchBarTheirs.png" : L"SearchBar.png";
			const auto     path = searchcfg::PluginFolder() / L"MenusRevamped" / L"Search" / file;
			static auto*   lib = ui::Class(L"/Script/Engine.KismetRenderingLibrary");
			ui::Call       c(lib ? lib->GetDefaultObject(false) : nullptr, L"ImportFileAsTexture2D");
			void*          fname = c.At("Filename");
			UE::UObject*   tex = nullptr;
			if (c && fname && std::filesystem::exists(path)) {
				c.Set("WorldContextObject", ui::PlayerController());
				auto* str = new (fname) UE::FString(path.wstring().c_str());
				tex = c.RunGuarded() ? c.Get<UE::UObject*>("ReturnValue") : nullptr;
				str->~FString();
			}
			if (!tex) {
				s_failed[slot] = true;
				logger::warn("own bar: the bar image could not be loaded ({}{}) - {}", path.string(), std::filesystem::exists(path) ? "" : ", missing", slot ? "the black one is used" : "a plain box is used");
				return slot ? BarTexture(menus::Menu::kInventory) : nullptr;
			}
			s_tex[slot] = reflect::Hold(tex);
			logger::debug("own bar: the bar image is loaded from {}", path.filename().string());
			return tex;
		}

		// a side of the container / barter menu is on the screen (a plain container shows only its own list)
		bool SideShownNow(menus::Menu a_side)
		{
			auto* content = container::SideContent(a_side);
			return content && ui::ShownOnScreen(content);
		}

		// the container / barter menu's bar: above the side's header (menu units, local to the menu's root canvas); the container's list alone on the
		// screen (the player's side not shown) has a place of its own
		bool ContainerBarRect(menus::Menu a_side, UE::UObject* a_canvas, double& a_x, double& a_y, double& a_w, double& a_h, bool a_log)
		{
			auto* content = container::SideContent(a_side);
			auto* header = content ? container::FindWidget(content, "inv_cont_header") : nullptr;
			double hx = 0, hy = 0, hw = 0, hh = 0, cx = 0, cy = 0, cw = 0, ch = 0;
			if (!header || !ui::Measure(header, hx, hy, hw, hh) || !ui::Measure(a_canvas, cx, cy, cw, ch) || hw < 10 || hh < 1) return false;
			const auto& c = searchcfg::Get();
			// the container's list without the tab row above it (a plain container, nothing to give) has a place of its own; with the tabs (beside yours in a
			// transfer or a barter, or beside an item's details) it sits like the barter's
			auto*       menuW = container::Widget();
			auto*       tabs = menuW ? container::FindWidget(menuW, a_side == menus::Menu::kContainerYours ? "cont_give_tabs" : "cont_take_tabs") : nullptr;
			const bool  take = a_side == menus::Menu::kContainerTheirs && !(tabs && ui::ShownOnScreen(tabs));
			const auto& p = take ? c.takeBar : a_side == menus::Menu::kContainerYours ? c.yoursBar : c.theirsBar;
			a_h = p.h;
			a_w = p.w > 0 ? p.w : hw + 2 * std::abs(p.x);
			a_x = hx - cx + p.x;
			a_y = hy - cy - a_h - 6 + p.y;
			if (cw > 100) g_canvasW = cw;
			if (a_log) {
				logger::debug("own bar: {}{} - header at {:.0f},{:.0f} {:.0f}x{:.0f}, canvas at {:.0f},{:.0f} {:.0f}x{:.0f} -> bar at {:.0f},{:.0f} {:.0f}x{:.0f}", menus::Name(a_side), take ? " (its list alone)" : "", hx, hy, hw, hh, cx, cy, cw, ch, a_x, a_y, a_w, a_h);
				for (const char* name : { "inv_cont_title", "inv_cont_filter", "inv_cont_listview_overlay" }) {
					double x = 0, y = 0, w = 0, h = 0;
					if (auto* o = ui::FindInTree(content, name); o && ui::Measure(o, x, y, w, h)) logger::debug("own bar:   {} at {:.0f},{:.0f} {:.0f}x{:.0f}", name, x, y, w, h);
				}
			}
			return true;
		}

		// the quests tab's bar sits where the Magic menu's does; what the canvas it went into and the quest panels measure is logged, to check that
		void LogQuestLayout(UE::UObject* a_canvas)
		{
			double x = 0, y = 0, w = 0, h = 0;
			if (ui::Measure(a_canvas, x, y, w, h)) logger::debug("own bar: the quests tab - the canvas the bar goes into is at {:.0f},{:.0f} {:.0f}x{:.0f}", x, y, w, h);
			if (auto* page = quests::Page(); page && ui::Measure(page, x, y, w, h)) logger::debug("own bar:   quests page at {:.0f},{:.0f} {:.0f}x{:.0f}", x, y, w, h);
			if (auto* panel = quests::ListPanel(); panel && ui::Measure(panel, x, y, w, h)) logger::debug("own bar:   quest list panel at {:.0f},{:.0f} {:.0f}x{:.0f}", x, y, w, h);
			if (auto* list = quests::ListWrapper(); list && ui::Measure(list, x, y, w, h)) logger::debug("own bar:   quest list at {:.0f},{:.0f} {:.0f}x{:.0f}", x, y, w, h);
		}

bool BuildBar(menus::Menu a_menu, UE::UObject* a_canvas)
{
			static int         s_n = 0;
			const std::wstring n = std::to_wstring(++s_n);
			const auto&        c = searchcfg::Get();
			const auto&        place = a_menu == menus::Menu::kMagic ? c.magicBar : a_menu == menus::Menu::kQuests ? c.questBar : c.inventoryBar;
			double             barX = place.x, barW = place.w, barH = place.h;
			double             barY = place.y;
			if (IsSide(a_menu) && !ContainerBarRect(a_menu, a_canvas, barX, barY, barW, barH, true)) return false;
				if (a_menu == menus::Menu::kQuests) LogQuestLayout(a_canvas);
			UE::UObject* slot = nullptr;
			auto*        frame = ui::AddToCanvas(a_canvas, L"/Script/UMG.Image", (L"IwmBarFrame" + n).c_str(), &slot);
			if (!frame || !slot) return false;
			ui::Vec2(slot, L"SetPosition", barX, barY);
			ui::Vec2(slot, L"SetSize", barW, barH);
			const std::int32_t z = 1000;
			ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			bool textured = false;
			if (auto* tex = BarTexture(a_menu)) {
				ui::Call b(frame, L"SetBrushFromTexture");
				b.Set("Texture", tex);
				b.Set("bMatchSize", false);
				textured = b.Run();
			}
			if (!textured) ui::RoundedBox(frame, false, kBarFill, kBarEdge, 2.0f);
			ui::Visible(frame, true);
			UE::UObject* lslot = nullptr;
			auto*        label = ui::AddToCanvas(a_canvas, L"/Script/UMG.TextBlock", (L"IwmBarText" + n).c_str(), &lslot);
			if (!label || !lslot) return false;
			ui::Vec2(lslot, L"SetAlignment", 0.5, 0.5);
			const bool yes = true;
			ui::CallFirst(lslot, L"SetAutoSize", &yes, sizeof(yes));
			ui::Vec2(lslot, L"SetPosition", barX + barW * 0.5, barY + barH * 0.5);
			const std::int32_t z2 = 1001;
			ui::CallFirst(lslot, L"SetZOrder", &z2, sizeof(z2));
			ui::Visible(label, true);
			BarWidgets& B = g_bar[static_cast<int>(a_menu)];
			B.canvas = reflect::Hold(a_canvas);
			B.frame = reflect::Hold(frame);
			B.frameSlot = reflect::Hold(slot);
			B.textured = textured;
			B.label = reflect::Hold(label);
			B.labelSlot = reflect::Hold(lslot);
			B.x = barX;
			B.y = barY;
			B.w = barW;
			B.h = barH;
			B.visible = true;
			B.shown = "\x01";   // matches nothing real: the first update draws it
			B.typing = false;
			logger::debug("own bar: built in the {}", menus::Name(a_menu));
			return true;
		}

		// a side that is not on the screen has no bar
		void HideBar(menus::Menu a_menu)
		{
			BarWidgets& B = g_bar[static_cast<int>(a_menu)];
			if (!B.visible) return;
			if (auto* f = reflect::Get(B.frame)) ui::Visible(f, false);
			if (auto* l = reflect::Get(B.label)) ui::Visible(l, false);
			B.visible = false;
			g_barPx[static_cast<int>(a_menu)].valid = false;
		}

		void UpdateBar(menus::Menu a_menu, Clock::time_point a_now)
		{
			BarWidgets& B = g_bar[static_cast<int>(a_menu)];
			auto*       label = reflect::Get(B.label);
			auto*       frame = reflect::Get(B.frame);
			if (!label || !frame) return;
			if (!B.visible) {
				ui::Visible(frame, true);
				ui::Visible(label, true);
				B.visible = true;
			}
			const bool        typing = g_typing.load() && g_typingMenu.load() == static_cast<int>(a_menu);
				if (!typing) {
					std::scoped_lock l(g_qMutex);
					g_selAll[static_cast<int>(a_menu) % 6] = false;
				}
				const std::string q = QueryOf(a_menu);
			const bool        caret = typing && (GetTickCount64() / 500) % 2 == 0;
			const bool        hint = q.empty() && !typing;
			const std::string show = hint ? B.hint : q + (typing ? (caret ? "|" : " ") : "");
			const bool        sel = typing && !q.empty() && SelAllOf(a_menu);   // selected: drawn in the bar's gold
				const std::string key = std::string(hint ? "#" : sel ? "*" : "=") + show;
			if (key != B.shown) {
				uix::SetText(label, show);
				uix::Ink(label, hint ? kBarHint : sel ? kBarFocus : kBarInk);
				uix::ApplyFont(label, searchcfg::Get().barFontSize, uix::NeedsFallbackFont(show));   // Greek, Cyrillic, kana, Han ...: a font that draws them
				B.shown = key;
			}
			if (typing != B.typing) {   // the bronze edge while it is being typed into
				if (!B.textured) ui::RoundedBox(frame, false, kBarFill, typing ? kBarFocus : kBarEdge, 2.0f);
				B.typing = typing;
			}
			if (a_now >= B.nextMeasure) {   // where it is in window pixels, for a click on it
				B.nextMeasure = a_now + 150ms;
				if (IsSide(a_menu)) {
					// yours says Player; the other side Seller in a barter (the merchant's gold row is on screen), else Container
					std::string want = "Player";
					if (a_menu == menus::Menu::kContainerTheirs) {
						auto* money = container::FindWidget(container::Widget(), "cont_take_money_verticalbox");
						want = money && ui::ShownOnScreen(money) ? "Seller" : "Container";
					}
					if (want != B.hint) {
						B.hint = want;
						logger::debug("own bar: the {}'s empty bar says {}", menus::Name(a_menu), want);
					}
					// the layout can change (the container's list alone, or beside yours): the bar follows its header
					auto*  canvas = reflect::Get(B.canvas);
					auto*  slot = reflect::Get(B.frameSlot);
					auto*  lslot = reflect::Get(B.labelSlot);
					double x = 0, y = 0, w = 0, h = 0;
					if (canvas && slot && lslot && ContainerBarRect(a_menu, canvas, x, y, w, h, false) &&
						(std::abs(x - B.x) > 0.5 || std::abs(y - B.y) > 0.5 || std::abs(w - B.w) > 0.5 || std::abs(h - B.h) > 0.5)) {
						ui::Vec2(slot, L"SetPosition", x, y);
						ui::Vec2(slot, L"SetSize", w, h);
						ui::Vec2(lslot, L"SetPosition", x + w * 0.5, y + h * 0.5);
						B.x = x;
						B.y = y;
						B.w = w;
						B.h = h;
						logger::debug("own bar: the {}'s bar moved to {:.0f},{:.0f} {:.0f}x{:.0f}", menus::Name(a_menu), x, y, w, h);
					}
				}
				double x = 0, y = 0, w = 0, h = 0;
				if (ui::Measure(frame, x, y, w, h)) {
					const double s = ViewportScale();
					std::scoped_lock l(g_qMutex);
					g_barPx[static_cast<int>(a_menu)] = { x * s, y * s, w * s, h * s, true };
				}
			}
		}

		// ---- the typing: the game window's messages ----
		// one more character on the end of a search text (UTF-8), unless it is not a printable one or the text is full
		bool AppendTyped(std::string& a_q, char32_t a_cp)
		{
			if (!textutil::Typable(a_cp) || textutil::CodePoints(a_q) >= kMaxText) return false;
			std::string add;
			textutil::AppendCodePoint(add, a_cp);
			if (a_q.size() + add.size() > kMaxBytes) return false;
			a_q += add;
			return true;
		}

		void PasteInto(std::string& a_q)
		{
			if (!OpenClipboard(nullptr)) return;
			if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
				if (const auto* w = static_cast<const wchar_t*>(GlobalLock(h))) {
					textutil::ForEachCodePoint(std::wstring_view(w),[&](char32_t a_cp) { AppendTyped(a_q, a_cp); });   // a line break and other controls are skipped
					GlobalUnlock(h);
				}
			}
			CloseClipboard();
		}

		void Typed(wchar_t a_c, menus::Menu a_menu)
		{
			std::scoped_lock l(g_qMutex);
			auto&            q = g_queries[static_cast<int>(a_menu) % 6];
			bool&            sel = g_selAll[static_cast<int>(a_menu) % 6];
			if (a_c == 8) {
				g_highSurrogate = 0;
				if (sel) q.clear();
				else textutil::PopCodePoint(q);   // a whole character, however many bytes it takes
				sel = false;
			} else if (a_c == 0x16) {   // Ctrl+V
				g_highSurrogate = 0;
				if (sel) q.clear();
				sel = false;
				PasteInto(q);
			} else if (a_c >= 0xD800 && a_c <= 0xDBFF) {
				g_highSurrogate = a_c;   // the first half of a character outside the BMP (an emoji, a rare CJK character): the second half is the next WM_CHAR
			} else {
				char32_t cp = a_c;
				if (a_c >= 0xDC00 && a_c <= 0xDFFF) {
					cp = g_highSurrogate ? 0x10000 + ((static_cast<char32_t>(g_highSurrogate) - 0xD800) << 10) + (a_c - 0xDC00) : 0;   // a lone second half is dropped
				}
				g_highSurrogate = 0;
				if (!textutil::Typable(cp)) return;
				if (sel) q.clear();   // a selected text is replaced by what is typed
				sel = false;
				AppendTyped(q, cp);
			}
		}

		bool InsideBar(menus::Menu a_menu, LPARAM a_l)
		{
			const int x = static_cast<short>(LOWORD(a_l)), y = static_cast<short>(HIWORD(a_l));
			std::scoped_lock l(g_qMutex);
			const auto& r = g_barPx[static_cast<int>(a_menu) % 6];
			return r.valid && x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
		}

		bool ControllerKeyboardUp() { return g_external.load(); }

		// the container / barter menu's side the mouse is over: yours is on the left (cont_give), the other on the right
		menus::Menu SideUnderCursor()
		{
			const bool yours = g_sideShown[static_cast<int>(menus::Menu::kContainerYours)].load(), theirs = g_sideShown[static_cast<int>(menus::Menu::kContainerTheirs)].load();
			if (yours && !theirs) return menus::Menu::kContainerYours;
			if (theirs && !yours) return menus::Menu::kContainerTheirs;
			POINT pt{};
			RECT  rc{};
			if (g_hwnd && GetCursorPos(&pt) && ScreenToClient(g_hwnd, &pt) && GetClientRect(g_hwnd, &rc) && pt.x > (rc.right - rc.left) / 2) return menus::Menu::kContainerTheirs;
			return menus::Menu::kContainerYours;
		}

		constexpr UINT_PTR kTimerId = 0x4D53;

		// ---- the IME (Japanese, Chinese ...) ----
		// A game window normally takes the input method away from the window (no text field wants it), so the keys of a Japanese or Chinese IME arrive as plain
		// Latin letters. While a bar is being typed into the window gets its default input context back; the committed text arrives in WM_IME_COMPOSITION
		// (GCS_RESULTSTR) and is typed into the bar from there, while the system's own composition and candidate windows draw the text still being composed.
		bool g_imeGiven = false;   // we gave the default input context back and have to take it away again

		void SyncIme(HWND a_h)
		{
			static bool s_on = false;
			const bool  want = g_typing.load() && searchcfg::Get().ime;
			if (want == s_on) return;
			s_on = want;
			if (want) {
				if (HIMC c = ImmGetContext(a_h)) {
					ImmReleaseContext(a_h, c);   // the window has one already: the game left it alone
				} else if (ImmAssociateContextEx(a_h, nullptr, IACE_DEFAULT)) {
					g_imeGiven = true;
					logger::info("ime: the window had no input context - the default one is on while a bar is typed into");
				}
			} else if (g_imeGiven) {
				ImmAssociateContext(a_h, nullptr);
				g_imeGiven = false;
			}
		}

		// the composition window under the bar's left end, the candidate list below it
		// Only from WM_IME_STARTCOMPOSITION, never from WM_IME_NOTIFY: ImmSetCompositionWindow / ImmSetCandidateWindow send a WM_IME_NOTIFY of their own, so
		// placing the windows on every notification loops without end (the stack overflow of 2026-10-20). The guard stops a nested call all the same.
		void PlaceImeWindows(HWND a_h, menus::Menu a_menu)
		{
			static bool s_in = false;
			if (s_in) return;
			s_in = true;
			struct Reset { ~Reset() { s_in = false; } } reset;
			HIMC c = ImmGetContext(a_h);
			if (!c) return;
			PxRect r;
			{
				std::scoped_lock l(g_qMutex);
				r = g_barPx[static_cast<int>(a_menu) % 6];
			}
			if (r.valid) {
				COMPOSITIONFORM cf{};
				cf.dwStyle = CFS_POINT;
				cf.ptCurrentPos = { static_cast<LONG>(r.x + 8), static_cast<LONG>(r.y + r.h * 0.15) };
				ImmSetCompositionWindow(c, &cf);
				CANDIDATEFORM cand{};
				cand.dwStyle = CFS_CANDIDATEPOS;
				cand.ptCurrentPos = { static_cast<LONG>(r.x), static_cast<LONG>(r.y + r.h) };
				ImmSetCandidateWindow(c, &cand);
			}
			ImmReleaseContext(a_h, c);
		}

		bool BarMessage(HWND a_h, UINT a_m, WPARAM a_w, LPARAM a_l, LRESULT& result)
		{

			if (OwnBarOn()) {
				SyncIme(a_h);
				auto       menu = menus::Active();
				const bool cont = menu == menus::Menu::kNone && container::Open();
				if (cont) menu = g_typing.load() ? static_cast<menus::Menu>(g_typingMenu.load()) : SideUnderCursor();   // the container / barter menu: the side typed into, else the one the mouse is on
				if (!cont && menu == menus::Menu::kNone && quests::Open()) menu = menus::Menu::kQuests;
				if (MenuEnabled(menu)) {
					if (g_typing.load()) {
						switch (a_m) {
						case WM_KEYDOWN:   // a key typed into the bar: nothing else sees it (the menu's hotkeys stay quiet)
							if (a_w == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {   // Ctrl+A: select all
															std::scoped_lock l(g_qMutex);
															g_selAll[static_cast<int>(menu) % 6] = !g_queries[static_cast<int>(menu) % 6].empty();
															return true;
														}
														if (a_w == VK_DELETE) {   // Delete clears a selected text
															std::scoped_lock l(g_qMutex);
															if (g_selAll[static_cast<int>(menu) % 6]) g_queries[static_cast<int>(menu) % 6].clear();
															g_selAll[static_cast<int>(menu) % 6] = false;
															return true;
														}
														if (a_w == VK_ESCAPE || a_w == VK_RETURN) g_typing = false;
							return true;
						case WM_CHAR:
							Typed(static_cast<wchar_t>(a_w), menu);
							return true;
						case WM_IME_COMPOSITION:
							if (!searchcfg::Get().ime) break;   // [Search] bIme=0: the game sees the IME messages as it always did
							if (a_l & GCS_RESULTSTR) {   // text the IME has committed (kana turned into kanji, a pinyin choice): typed into the bar
								if (HIMC c = ImmGetContext(a_h)) {
									const LONG bytes = ImmGetCompositionStringW(c, GCS_RESULTSTR, nullptr, 0);
									if (bytes > 0) {
										std::wstring w(static_cast<std::size_t>(bytes) / sizeof(wchar_t), L'\0');
										ImmGetCompositionStringW(c, GCS_RESULTSTR, w.data(), bytes);
										for (const wchar_t ch : w) Typed(ch, menu);
									}
									ImmReleaseContext(a_h, c);
								}
							}
							// the system draws what is still being composed; without the result flags it makes no WM_IME_CHAR / WM_CHAR of the text we took
							{ result = DefWindowProcW(a_h, a_m, a_w, a_l & ~static_cast<LPARAM>(GCS_RESULTSTR | GCS_RESULTCLAUSE | GCS_RESULTREADSTR | GCS_RESULTREADCLAUSE)); return true; }
						case WM_IME_STARTCOMPOSITION:
							if (!searchcfg::Get().ime) break;
							PlaceImeWindows(a_h, menu);
							{ result = DefWindowProcW(a_h, a_m, a_w, a_l); return true; }
						case WM_IME_NOTIFY:
						case WM_IME_ENDCOMPOSITION:
						case WM_IME_SETCONTEXT:
							if (!searchcfg::Get().ime) break;
							{ result = DefWindowProcW(a_h, a_m, a_w, a_l); return true; }
						case WM_IME_CHAR:   // the committed text came through WM_IME_COMPOSITION: nothing is typed twice
							if (!searchcfg::Get().ime) break;
							return true;
						case WM_LBUTTONDOWN:
							if (cont && !InsideBar(menu, a_l)) {   // the other side's bar: typing moves there
								const auto other = menu == menus::Menu::kContainerYours ? menus::Menu::kContainerTheirs : menus::Menu::kContainerYours;
								if (MenuEnabled(other) && InsideBar(other, a_l)) {
									g_typingMenu = static_cast<int>(other);
									return true;
								}
							}
							if (!InsideBar(menu, a_l)) g_typing = false;   // a click elsewhere ends it and goes on to the game
							break;
						default: break;
						}
					} else if (!ControllerKeyboardUp()) {
						const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
						if (a_m == WM_KEYDOWN && a_w == 'F' && ctrl && !alt) {
							g_typingMenu = static_cast<int>(menu);
							g_typing = true;
							SyncIme(a_h);
							return true;
						}
						if (a_m == WM_LBUTTONDOWN) {
							const auto other = !cont ? menus::Menu::kNone : menu == menus::Menu::kContainerYours ? menus::Menu::kContainerTheirs : menus::Menu::kContainerYours;
							for (const auto m : { menu, other }) {
								if (m != menus::Menu::kNone && MenuEnabled(m) && InsideBar(m, a_l)) {
									g_typingMenu = static_cast<int>(m);
									g_typing = true;
									SyncIme(a_h);
									return true;
								}
							}
						}
					}
				} else if (g_typing.load()) {
					g_typing = false;
				}
			}
			return false;
		}




	}

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
		const auto& categories = id <= 4 ? gearfilter::Evaluate(id, source, state.lifecycle.generation) : source;
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

	void BarTick(menus::Menu a_menu, Clock::time_point a_now)
	{
		if (!OwnBarOn() || !reflect::Ok()) return;

		if (!MenuEnabled(a_menu)) {
			if (g_typingMenu.load() == static_cast<int>(a_menu)) g_typing = false;
			if (IsSide(a_menu)) HideBar(a_menu);
			return;
		}
BarWidgets& B = g_bar[static_cast<int>(a_menu)];
const bool  have = reflect::Get(B.frame) && reflect::Get(B.label);
		if (!have || a_now >= B.nextLook) {   // built, and looked at twice a second: it must still sit in the menu's canvas (a menu that was rebuilt or replaced does not take it along)
			B.nextLook = a_now + 500ms;
			auto* canvas = BarCanvasFor(a_menu, !have);
			if (!canvas) {
				if (!have) return;
			} else if (!have || canvas != reflect::Get(B.canvas)) {
				if (have) logger::debug("own bar: the {}'s canvas is another one now - the bar is built again", menus::Name(a_menu));
				for (auto* w : { reflect::Get(B.frame), reflect::Get(B.edge), reflect::Get(B.label) }) {
					if (w) {
						ui::Call rm(w, L"RemoveFromParent");
						rm.Run();
					}
				}
				if (!BuildBar(a_menu, canvas)) return;
			}
		}
		UpdateBar(a_menu, a_now);
	}

	void Tick(menus::Menu a_menu)
	{
		const auto now = Clock::now();
		const bool cont = a_menu == menus::Menu::kNone && container::Open();   // the container / barter menu: a bar and a list on each side
		{   // which of its sides are on the screen: a plain container shows only its own list (nothing to give)
			static auto nextShown = Clock::now();
			if (!cont) {
				g_sideShown[static_cast<int>(menus::Menu::kContainerYours)] = false;
				g_sideShown[static_cast<int>(menus::Menu::kContainerTheirs)] = false;
			} else if (now >= nextShown) {
				nextShown = now + 100ms;
				for (const auto side : { menus::Menu::kContainerYours, menus::Menu::kContainerTheirs }) {
					const bool shown = SideShownNow(side);
					if (shown != g_sideShown[static_cast<int>(side)].load()) logger::debug("container menu: the {} is {}", menus::Name(side), shown ? "on the screen" : "not on the screen");
					g_sideShown[static_cast<int>(side)] = shown;
				}
			}
		}
const bool quest = !cont && a_menu == menus::Menu::kNone && quests::Open();   // the quests tab: a bar and the quest list
if (g_typing.load()) {
	const auto typed = static_cast<menus::Menu>(g_typingMenu.load());
	if ((typed != a_menu && !(cont && IsSide(typed)) && !(quest && typed == menus::Menu::kQuests)) || g_external.load()) g_typing = false;
}
		if (cont) {
			for (const auto side : { menus::Menu::kContainerYours, menus::Menu::kContainerTheirs }) {
				BarTick(side, now);
				FilterTick(side, now);
			}
		} else if (quest) {
			BarTick(menus::Menu::kQuests, now);
			FilterTick(menus::Menu::kQuests, now);
		} else {
			BarTick(a_menu, now);
			FilterTick(a_menu, now);
		}
}

	void SetWindow(HWND window) { g_hwnd = window; }
	bool HandleMessage(HWND window, UINT message, WPARAM key, LPARAM position, LRESULT& result)
	{
		return BarMessage(window, message, key, position, result);
	}

	void SetGameThread(DWORD a_id) { g_gameThread = a_id; }
	bool Enabled(menus::Menu a_menu) { return MenuEnabled(a_menu); }
	std::string GetText(menus::Menu a_menu) { return QueryOf(a_menu); }
	void SetText(menus::Menu a_menu, const std::string& a_text)
	{
		std::string s;   // a_text is UTF-8 (the keyboard plugin sends plain ASCII); what cannot be typed is left out
		textutil::ForEachCodePoint(textutil::Wide(a_text), [&](char32_t a_cp) { AppendTyped(s, a_cp); });
		SetQuery(a_menu, s);
	}
	bool PanelOnLeft(menus::Menu a_menu)
	{
		const auto& B = g_bar[static_cast<int>(a_menu) % 6];
		if (IsSide(a_menu) && B.w > 0) return B.x + B.w * 0.5 < g_canvasW * 0.5;
		return a_menu != menus::Menu::kContainerTheirs;
	}

	int Context()
{
		const auto m = menus::Active();
		if (m != menus::Menu::kNone) return static_cast<int>(m);
		if (searchcfg::Get().container && container::Open()) return 3;
				return searchcfg::Get().quests && quests::Open() ? 5 : 0;
	}
	bool Typing() { return g_typing.load(); }

	bool CursorOverBar()
	{
		POINT pt{};
		if (!g_hwnd || !GetCursorPos(&pt) || !ScreenToClient(g_hwnd, &pt)) return false;
		std::scoped_lock l(g_qMutex);
		for (const auto& r : g_barPx)
			if (r.valid && pt.x >= r.x && pt.x <= r.x + r.w && pt.y >= r.y && pt.y <= r.y + r.h) return true;
		return false;
	}

	void SetExternalInput(bool a_open)
{
		g_external = a_open;
		if (a_open) g_typing = false;
	}
}
