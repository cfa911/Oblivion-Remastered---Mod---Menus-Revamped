// Own sort bars replacing the native sorts (owner 2026-11-07; KB mods/menus-revamped/evidence/own-sorts-20261107.md).
//
// Order: the native refill hands the list Blueprint an array of OriginalInventoryMenuItemProperties structs and the
// Blueprint copies Items[i] into existing row i (InventoryListView.UpdateListView, SetProperties in place). Permuting
// that array BEFORE the Blueprint body runs (pe::Watch) makes the native publication ours: no second rebuild, no
// flash. Same idea as Alchemy's SortNativeIngredientSource (whole structs permuted in their own allocation, each
// owned member present exactly once).
// Republish after a click: the view model's own sort setter (the native buttons' path) makes the game refill the
// list, and the refill hook orders it. The native sort type itself no longer matters: we alternate Name/Price so
// every click is a real change.

#include "sorting/PickerInternal.h"

#include "core/Config.h"
#include "core/Runtime.h"
#include "engine/ContainerMenu.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/Ui.h"

#include <unordered_set>

namespace alchemy::detail
{
	bool PadInput();   // AlchemyLayout.cpp: the game's own input device (ECommonInputType 1 = Gamepad)
}

namespace sorting::detail
{
	namespace
	{
		struct RawArray { std::uint8_t* data; std::int32_t num; std::int32_t max; };
		// Category sort: item keys a grouped refill could not place (no classification yet), per FilterState slot
		// (0 Inventory, 1 player side, 2 container side). When the classification lands with any of them, one republish.
		std::vector<std::uint64_t> g_waiting[3];

		struct Layout
		{
			bool ok = false;
			int  size = 0, name = -1, price = -1, weight = -1, damage = -1, armor = -1, health = -1;
		};

		const Layout& Props()
		{
			static Layout l;
			static bool   tried = false;
			if (tried) return l;
			tried = true;
			auto* st = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Altar.OriginalInventoryMenuItemProperties"));
			if (!st) { logger::warn("Menus Revamped: item properties struct not found; own sorts keep the game's order"); return l; }
			l.size = st->propertiesSize;
			l.name = reflect::Offset(st, "Name");
			l.price = reflect::Offset(st, "Price");
			l.weight = reflect::Offset(st, "Weight");
			l.damage = reflect::Offset(st, "WeaponDamage");
			l.armor = reflect::Offset(st, "ArmorRating");
			l.health = reflect::Offset(st, "Health");
			bool floats = true;
			for (const char* f : { "Price", "Weight", "WeaponDamage", "ArmorRating", "Health" }) floats = floats && ui::SizeOf(st, f) == 4;
			l.ok = l.size > 0 && l.size < 0x400 && l.name >= 0 && l.price >= 0 && l.weight >= 0 && l.damage >= 0 && l.armor >= 0 &&
			       l.health >= 0 && floats && ui::SizeOf(st, "Name") == static_cast<std::int32_t>(sizeof(UE::FText));
			if (!l.ok) logger::warn("Menus Revamped: item properties layout unexpected (size {}); own sorts keep the game's order", l.size);
			return l;
		}

		Picker* ActivePicker(int a_kind);

		// Magic rows (KB probing/snippets/mr-hover-refill.md, tab-pages "Property"): Name FText, Property float = cost.
		struct MagicLayout { bool ok = false, groups = false; int size = 0, name = -1, cost = -1, category = -1, school = -1, effectValue = -1, type = -1, index = -1; };

		const MagicLayout& MagicProps()
		{
			static MagicLayout l;
			static bool        tried = false;
			if (tried) return l;
			tried = true;
			auto* st = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Altar.LegacyMagicMenuItemProperties"));
			if (!st) { logger::warn("Menus Revamped: Magic item struct not found; own Magic sorts keep the game's order"); return l; }
			l.size = st->propertiesSize;
			l.name = reflect::Offset(st, "Name");
			l.cost = reflect::Offset(st, "Property");   // cost on spell rows, magnitude on Active Effects rows
			l.category = reflect::Offset(st, "Category");
			l.school = reflect::Offset(st, "School");
			l.effectValue = reflect::Offset(st, "EffectValue");
			l.ok = l.size > 0 && l.size < 0x400 && l.name >= 0 && l.cost >= 0 && ui::SizeOf(st, "Property") == 4 &&
			       l.category >= 0 && l.school >= 0 && l.effectValue >= 0 &&
			       ui::SizeOf(st, "Category") == 1 && ui::SizeOf(st, "School") == 1 && ui::SizeOf(st, "EffectValue") == 1 &&
			       ui::SizeOf(st, "Name") == static_cast<std::int32_t>(sizeof(UE::FText));
			if (!l.ok) logger::warn("Menus Revamped: Magic item layout unexpected (size {}); own Magic sorts keep the game's order", l.size);
			// Category sort: range (Type) and InventoryIndex (KB probing/snippets/mr-hover-refill.md: Type+41, InventoryIndex+44).
			l.type = reflect::Offset(st, "Type");
			l.index = reflect::Offset(st, "InventoryIndex");
			l.groups = l.ok && l.type >= 0 && l.index >= 0 && ui::SizeOf(st, "Type") == 1 && ui::SizeOf(st, "InventoryIndex") == 4;
			if (l.ok && !l.groups) logger::warn("Menus Revamped: Magic item Type/InventoryIndex not found; the Magic category sort keeps A-Z");
			return l;
		}

		// The Magic page at refill time: 0..3 spell pages, 4 Active Effects, -1 unknown.
		int MagicPageNow(Picker& a_picker)
		{
			auto* menu = reflect::Get(a_picker.menu);
			auto** vm = menu ? reflect::At<UE::UObject*>(menu, reflect::Offset(menu->GetClass(), "VMagicMenuViewModel")) : nullptr;
			ui::Call page(vm ? *vm : nullptr, L"GetCurrentPage");
			return page && page.Size("ReturnValue") == 1 && page.Run() ? page.Get<std::uint8_t>("ReturnValue") : -1;
		}

		// Permute whole structs in their own allocation (each owned member stays present exactly once).
		void Permute(RawArray& a_items, std::size_t a_stride, const std::vector<int>& a_order)
		{
			bool changed = false;
			for (std::size_t i = 0; i < a_order.size(); ++i) changed |= i != static_cast<std::size_t>(a_order[i]);
			if (!changed) return;
			std::vector<std::uint8_t> ordered(a_order.size() * a_stride);
			for (std::size_t i = 0; i < a_order.size(); ++i)
				std::memcpy(ordered.data() + i * a_stride, a_items.data + static_cast<std::size_t>(a_order[i]) * a_stride, a_stride);
			std::memcpy(a_items.data, ordered.data(), ordered.size());
		}

		void OnMagicRefill(UE::UFunction* a_fn, void* a_params)
		{
			auto* effects = ActivePicker(6);   // Active Effects host (page 4)
			auto* picker = effects ? effects : ActivePicker(1);
			const auto& l = MagicProps();
			if (!picker || !l.ok) return;
			const int off = reflect::Offset(a_fn, "NewList");
			if (off < 0 || ui::SizeOf(a_fn, "NewList") != static_cast<std::int32_t>(sizeof(RawArray))) return;
			auto& items = *reinterpret_cast<RawArray*>(static_cast<std::uint8_t*>(a_params) + off);
			const int page = MagicPageNow(*picker);
			const bool active = picker->kind == 6;
			if (active ? page != 4 : (page < 0 || page >= 4)) return;
			if (!items.data || items.num < 2 || items.num > 4000 || items.max < items.num) return;
			const auto state = active ? itemsort::Effective(picker->SortAt(5), itemsort::ActiveKeys()) :
			                            itemsort::Effective(picker->SortAt(Picker::kMagicSorts + page), itemsort::MagicKeys());
			const auto stride = static_cast<std::size_t>(l.size);
			std::vector<itemsort::Row> rows(static_cast<std::size_t>(items.num));
			const bool grouped = !active && state.group >= 0 && l.groups;
			const auto lists = grouped ? ReadMagicLists() : MagicLists{};   // the same masks the Magic filters use, read now
			for (std::size_t i = 0; i < rows.size(); ++i) {
				const auto* e = items.data + i * stride;
				if (grouped) {
					std::int32_t index = 0;
					std::memcpy(&index, e + l.index, 4);
					rows[i].group = itemsort::MagicGroupRank(state.group, MagicRowMask(e[l.type], e[l.school], index, lists));
				}
				rows[i].name = reflect::Text(*reinterpret_cast<const UE::FText*>(e + l.name));
				std::memcpy(&rows[i].cost, e + l.cost, 4);
				rows[i].magnitude = rows[i].cost;   // the same Property column: magnitude on Active Effects rows
				rows[i].category = static_cast<float>(e[l.category]);
				rows[i].school = static_cast<float>(e[l.school]);
				rows[i].effectValue = static_cast<float>(e[l.effectValue]);
			}
			Permute(items, stride, itemsort::Order(rows, state));
		}

		Picker* ActivePicker(int a_kind)
		{
			for (auto& picker : g_pickers)
				if (picker.kind == a_kind && picker.active.load()) return &picker;
			return nullptr;
		}

		// The side's tab at refill time (the refill of a tab change can come before the picker's tick sees the tab).
		itemtags::Tab TabNow(Picker& a_picker)
		{
			auto tab = a_picker.tab;
			if (a_picker.kind >= 3) {
				itemtags::TabFromPage(ContainerPage(a_picker.kind), tab);
			} else if (auto* menu = reflect::Get(a_picker.menu)) {
				auto** vm = reflect::At<UE::UObject*>(menu, reflect::Offset(menu->GetClass(), "VInventoryMenuViewModel"));
				auto* page = vm && *vm ? reflect::At<std::uint8_t>(*vm, reflect::Offset((*vm)->GetClass(), "CurrentPageInventory")) : nullptr;
				if (page) itemtags::TabFromPage(*page, tab);
			}
			return tab;
		}


		void OnRefill(UE::UObject*, UE::UFunction* a_fn, void* a_params)
		{
			if (!a_fn || !a_params) return;
			const auto name = pe::FunctionName(a_fn);
			if (name == "UpdateCurrentPageInventory") { OnMagicRefill(a_fn, a_params); return; }
			// Container/barter: the native-called custom events; they run UpdateGive/TakeListView inside the Blueprint (a script
			// call that ProcessEvent never sees; KB internals "Hover call chains" table, measured 2026-11-07).
			const int  kind = name == "UpdateListViewItems" ? 0 : name == "UpdateInventoryListView" ? 3 : name == "UpdateContainerListView" ? 4 : -1;
			if (kind < 0) return;
			auto* picker = ActivePicker(kind);
			const auto& l = Props();
			if (!picker || !l.ok) return;
			const int off = reflect::Offset(a_fn, "Items");
			if (off < 0 || ui::SizeOf(a_fn, "Items") != static_cast<std::int32_t>(sizeof(RawArray))) return;
			auto& items = *reinterpret_cast<RawArray*>(static_cast<std::uint8_t*>(a_params) + off);
			if (!items.data || items.num < 2 || items.num > 4000 || items.max < items.num) return;
			const auto tab = TabNow(*picker);
			const auto state = itemsort::Effective(picker->SortAt(static_cast<int>(tab)), itemsort::Keys(tab));
			const auto stride = static_cast<std::size_t>(l.size);
			std::vector<itemsort::Row> rows(static_cast<std::size_t>(items.num));
			auto& waiting = g_waiting[kind == 0 ? 0 : kind - 2];
			waiting.clear();
			for (std::size_t i = 0; i < rows.size(); ++i) {
				const auto* e = items.data + i * stride;
				auto& row = rows[i];
				if (state.group >= 0) {   // category sort: the row's classification, cached by item key
					std::uint64_t tags = 0;
					const auto key = RowKey(e);
					if (CachedRowTags(key, tags)) row.group = itemsort::GroupRank(tab, static_cast<std::uint32_t>(tags));
					else if (key) waiting.push_back(key);
				}
				row.name = reflect::Text(*reinterpret_cast<const UE::FText*>(e + l.name));
				std::memcpy(&row.price, e + l.price, 4);
				std::memcpy(&row.weight, e + l.weight, 4);
				std::memcpy(&row.damage, e + l.damage, 4);
				std::memcpy(&row.armor, e + l.armor, 4);
				std::memcpy(&row.health, e + l.health, 4);
			}
			Permute(items, stride, itemsort::Order(rows, state));
		}

		bool SetNativeSort(UE::UObject* a_vm, const wchar_t* a_setter, std::uint8_t a_type)
		{
			ui::Call set(a_vm, a_setter);
			if (!set) return false;
			for (const char* param : { "SortType", "NewSortType", "InSortType", "Type", "Value" }) {
				if (set.Size(param) != 1) continue;
				set.Set(param, a_type);
				return set.Run();
			}
			return false;
		}
	}

	void TagsLanded(unsigned a_slot)
	{
		if (a_slot > 2 || g_waiting[a_slot].empty()) return;
		std::uint64_t tags = 0;
		const bool placed = std::any_of(g_waiting[a_slot].begin(), g_waiting[a_slot].end(), [&](std::uint64_t key) { return CachedRowTags(key, tags); });
		g_waiting[a_slot].clear();   // rows that never classify do not republish again (no loop)
		if (!placed) return;
		if (auto* picker = ActivePicker(a_slot == 0 ? 0 : static_cast<int>(a_slot) + 2)) picker->pendingRepublish = true;
	}

	Picker* Picker::SortHost()
	{
		if (kind == 2 || kind == 5) return g_pickers[0].kind == 1 && g_pickers[0].active.load() ? &g_pickers[0] : nullptr;
		return this;
	}

	void Picker::SeedSorts()
	{
		if (!OwnSorts() || kind == 6 || kind < 0 || (seededKinds & (1u << kind))) return;
		seededKinds |= 1u << kind;
		const bool on = mrconfig::Get().picker.categorySort;
		const int first = kind == 1 ? kMagicSorts : 0, count = kind == 1 ? 4 : 5;
		for (int slot = first; slot < first + count; ++slot) sorts[slot] = itemsort::Default(on, kind);   // Magic default: school
	}

	// The filter shows its active category sort like an active sort button: the game's parchment and the arrow.
	void Picker::PaintGroup()
	{
		auto* host = active.load() ? SortHost() : nullptr;
		const auto state = host && host->OwnSorts() ? host->SortAt(host->SortSlot()) : itemsort::State{};
		const bool on = state.group >= 0 && state.group == kind && kind != 6;
		if (auto* back = reflect::Get(groupBack)) ui::Visible(back, on && !controllerFocus);
		if (auto* arrow = reflect::Get(groupArrow)) {
			if (on) TextureBrush(arrow, state.descending ? itemsort::kArrowDown : itemsort::kArrowUp);
			ui::Visible(arrow, on);
		}
		ui::Float(reflect::Get(headerImage), L"SetRenderOpacity", on || choice || category ? 1.0f : 0.5f);
	}

	const wchar_t* AllTexture(int a_kind)
	{
		return a_kind == 1 ? L"/Game/Art/UI/Icons/Icon_Stats/T_Property_1_school.T_Property_1_school" :
			a_kind == 5 ? L"/Game/Art/UI/Icons/Icon_Stats/T_Property_1_LesserPower.T_Property_1_LesserPower" : nullptr;
	}

	void WatchRefills(Picker& a_picker)
	{
		static std::unordered_set<UE::UClass*> s_watched;
		UE::UObject* owner = nullptr;
		if (a_picker.kind == 0) {
			if (auto* menu = reflect::Get(a_picker.menu)) {
				auto** part = reflect::At<UE::UObject*>(menu, reflect::Offset(menu->GetClass(), "inv_main_part"));
				owner = part ? *part : nullptr;
			}
		} else if (a_picker.kind == 1 || a_picker.kind == 6) {
			owner = reflect::Get(a_picker.menu);   // WBP_ModernMenu_MagicMenu.UpdateCurrentPageInventory(NewList)
		} else {
			owner = container::Widget();
		}
		if (!owner || s_watched.contains(owner->GetClass())) return;
		if (pe::Watch(owner->GetClass(), &OnRefill)) s_watched.insert(owner->GetClass());
		else logger::warn("Menus Revamped: list refill of kind {} not watchable; own sorts keep the game's order", a_picker.kind);
	}

	void Republish(Picker& a_picker)
	{
		static std::uint8_t s_next[5] = {};
		UE::UObject*   vm = nullptr;
		const wchar_t* setter = L"SetSortTypeInventory";
		if (a_picker.kind == 0) {
			if (auto* menu = reflect::Get(a_picker.menu)) {
				auto** field = reflect::At<UE::UObject*>(menu, reflect::Offset(menu->GetClass(), "VInventoryMenuViewModel"));
				vm = field ? *field : nullptr;
			}
		} else if (a_picker.kind == 1 || a_picker.kind == 6) {
			if (auto* menu = reflect::Get(a_picker.menu)) {
				auto** field = reflect::At<UE::UObject*>(menu, reflect::Offset(menu->GetClass(), "VMagicMenuViewModel"));
				vm = field ? *field : nullptr;
			}
			setter = L"SetSortType";
		} else if (auto* menu = container::Widget()) {
			auto** field = reflect::At<UE::UObject*>(menu, reflect::Offset(menu->GetClass(), "VContainerMenuViewModel"));
			vm = field ? *field : nullptr;
			if (a_picker.kind == 4) setter = L"SetSortTypeContainer";
		}
		auto& next = s_next[a_picker.kind];
		// Two sort types every page offers, alternated so each click is a real change: Inventory Name 0 / Price 1;
		// Magic spell pages Name 0 / Cost 6 (KB tab-pages "Magic sort types per page").
		const std::uint8_t other = a_picker.kind == 1 ? static_cast<std::uint8_t>(itemsort::NativeSortType(itemsort::Key::Cost)) :
			a_picker.kind == 6 ? static_cast<std::uint8_t>(itemsort::NativeSortType(itemsort::Key::Category)) : 1;   // page 4 offers 0 and 2
		next = next ? 0 : other;
		// 1st choice: the Blueprint path the native sort buttons use (parameter `SortType` in the offline decode, KB
		// evidence own-sorts-20261107 "Native sort call chain"); it calls the view model setter itself.
		UE::UObject*   bpOwner = nullptr;
		const wchar_t* bpEvent = nullptr;
		if (a_picker.kind == 0) {
			if (auto* menu = reflect::Get(a_picker.menu)) {
				auto** part = reflect::At<UE::UObject*>(menu, reflect::Offset(menu->GetClass(), "inv_main_part"));
				bpOwner = part ? *part : nullptr;
			}
			bpEvent = L"OnSortTypeUpdated";
		} else if (a_picker.kind == 1 || a_picker.kind == 6) {
			bpOwner = reflect::Get(a_picker.menu);
			bpEvent = L"BndEvt__WBP_ModernMenu_MagicMenu_Filters_K2Node_ComponentBoundEvent_0_OnSortTypeUpdated__DelegateSignature";
		} else {
			bpOwner = container::Widget();
			bpEvent = a_picker.kind == 3 ?
				L"BndEvt__WBP_ModernMenu_Container_cont_give_maincontent_K2Node_ComponentBoundEvent_40_OnSortTypeUpdated__DelegateSignature" :
				L"BndEvt__WBP_ModernMenu_Container_cont_take_maincontent_K2Node_ComponentBoundEvent_43_OnSortTypeUpdated__DelegateSignature";
		}
		bool ok = false;
		if (bpOwner) {
			ui::Call event(bpOwner, bpEvent);
			if (event && event.Size("SortType") == 1 && event.Set("SortType", next)) ok = event.Run();
		}
		if (!ok && vm) ok = SetNativeSort(vm, setter, next);
		if (!ok) logger::warn("Menus Revamped: own sort could not ask the game to refresh the list (kind {})", a_picker.kind);
	}

	bool IsOwnSortHeader(UE::UObject* a_widget)
	{
		if (!a_widget) return false;
		for (const auto& picker : g_pickers)
			for (const auto& h : picker.sortHeaders)
				if (reflect::Get(h) == a_widget) return true;
		return false;
	}

	void Picker::HideNativeSorts()
	{
		if (!active.load() || !OwnSorts()) return;
		for (const auto& handle : nativeSorts) {
			auto* native = reflect::Get(handle);
			if (!native) continue;
			ui::Call visible(native, L"IsVisible");
			if (visible && visible.Size("ReturnValue") == 1 && visible.Run() && !visible.Get<bool>("ReturnValue")) continue;
			ui::Call hide(native, L"SetVisibility");
			if (hide && hide.Size("InVisibility") == 1 && hide.Set("InVisibility", static_cast<std::uint8_t>(1))) hide.Run();   // Collapsed
		}
	}

	void Picker::ShowQuickLootHint()
	{
		if (kind != 4 || !active.load()) return;
		const bool quickLoot = !container::SideShown(3);   // Transfer and barter show the player side; the game owns the glyph there
		auto* hint = reflect::Get(quickLootHint);
		if (!hint) {
			if (!quickLoot) return;
			auto* side = container::SideContent(kind);
			auto* filter = side ? container::FindWidget(side, "inv_cont_filter") : nullptr;
			// Not named Filter_InputHint_Horizontalbox here (that is Magic's; R10 log): find the game's glyph by its class.
			hint = filter ? ui::FindByClassPrefix(filter, "WBP_ModernPrefab_ControllerInputHint") : nullptr;
			if (!hint) {
				static bool s_warned = false;
				if (!s_warned && filter) { s_warned = true; logger::warn("Menus Revamped: container filter Y glyph (WBP_ModernPrefab_ControllerInputHint) not found"); }
				return;
			}
			quickLootHint = reflect::Hold(hint);
			quickLootHintShown = -1;
		}
		if (!quickLoot) { quickLootHintShown = -1; return; }   // hand it back to the game untouched
		const int want = alchemy::detail::PadInput() ? 1 : 0;
		if (want == 1) {
			// The glyph and any collapsed parent up to the filter (the game may hide either again: compare with the real state).
			auto* filter = container::FindWidget(container::SideContent(kind), "inv_cont_filter");
			for (auto* w = hint; w && w != filter;) {
				ui::Call visible(w, L"IsVisible");
				if (visible && visible.Size("ReturnValue") == 1 && visible.Run() && !visible.Get<bool>("ReturnValue")) {
					ui::Visible(w, true);
					const bool known = std::any_of(quickLootShown.begin(), quickLootShown.end(), [&](const auto& h) { return reflect::Get(h) == w; });
					if (!known) quickLootShown.push_back(reflect::Hold(w));
				}
				ui::Call parent(w, L"GetParent");
				w = parent && parent.Run() ? parent.Get<UE::UObject*>("ReturnValue") : nullptr;
			}
		} else if (quickLootHintShown != 0) {
			ui::Visible(hint, false);   // mouse/keyboard: as the game shows quick loot (only what we opened is closed again)
			for (const auto& h : quickLootShown) if (auto* w = reflect::Get(h); w && w != hint) ui::Visible(w, false);
			quickLootShown.clear();
		}
		quickLootHintShown = want;
	}

	void Picker::ShowOwnWidgets()
	{
		if (!active.load()) return;
		for (const auto& handle : { enchantedHeader, stolenHeader, header })
			if (auto* w = reflect::Get(handle)) ui::Visible(w, true);
		if (kind == 6) {   // the Active Effects host has sorts only: its category header stays hidden
			if (auto* h = reflect::Get(header)) ui::Visible(h, false);
		}
		for (int k = 0; k < kSorts; ++k)
			if (auto* w = reflect::Get(sortHeaders[k])) ui::Visible(w, OwnSorts() && Offers(static_cast<itemsort::Key>(k)));
	}

	void Picker::PaintSorts()
	{
		if (!OwnSorts()) return;
		paintedSlot = SortSlot();
		const auto state = itemsort::Effective(SortAt(paintedSlot), SortKeys());
		for (auto& peer : g_pickers) if (peer.SortHost() == this) peer.PaintGroup();
		for (int k = 0; k < kSorts; ++k) {
			const bool on = state.group < 0 && state.key == static_cast<itemsort::Key>(k), focus = sortFocus == k;
			ui::RoundedBox(reflect::Get(sortFrames[k]), false, focus ? pickerhighlight::kFill.data() : kClear, kClear,
				pickerhighlight::kOutlineWidth, pickerhighlight::kCornerRadius);
			if (auto* back = reflect::Get(sortBacks[k])) ui::Visible(back, on && !focus);
			if (auto* image = reflect::Get(sortImages[k])) ui::Float(image, L"SetRenderOpacity", on ? 1.0f : 0.5f);
			if (auto* arrow = reflect::Get(sortArrows[k])) {
				if (on) TextureBrush(arrow, state.descending ? itemsort::kArrowDown : itemsort::kArrowUp);   // the Alchemy arrows
				ui::Visible(arrow, on);
			}
		}
	}

	int Picker::SortFocused()
	{
		if (!OwnSorts() || !SortCarouselFocused()) return -1;
		ui::Call focused(reflect::Get(carousel), L"GetFocusedElement");
		if (!focused || focused.Size("ReturnValue") != 8 || !focused.RunGuarded()) return -1;
		auto* element = focused.Get<UE::UObject*>("ReturnValue");
		for (int k = 0; k < kSorts; ++k)
			if (element && reflect::Get(sortHeaders[k]) == element) return k;
		return -1;
	}
}
