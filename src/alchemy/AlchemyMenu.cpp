// Alchemy menu: ingredient sort (IMP-MR-005), effect filter counts (IMP-MR-004), active-filter line (IMP-MR-006).
// Facts used (knowledge base docs/game-internals.md "Alchemy probe run 3/4", build 1.512.105.0):
// - the menu WBP_ModernMenu_AlchemyMenu_C has BP fields IngredientsListView, Alchemy_Filters, AppliedFilters_Text,
//   Alchemy_NameBox, VAlchemyMenuViewModel; list wrappers hold the inner VModernListView in the field ListView;
// - ingredient list items are VLegacyIngredientItem with a LegacyIngredientItemProperties struct in "Properties";
//   the view model's ItemsList is a TArray of those structs (every ingredient, filtered or not);
// - a filter row (WBP_ModernMenu_FilterEffectEntry_C) shows its effect in EffectText > text_block; its list item
//   (VModernAlchemyEffect, no reflected fields) holds the effect name as an FText at +0x38 (validated at run time
//   against the row before it is trusted);
// - a filter click runs the Filters widget's ListView_Filters OnItemClicked bound event, then UpdateItemsList.
#include "alchemy/AlchemyInternal.h"

namespace alchemy
{
	namespace detail
	{

		void OnIngredientEntryGenerated(UE::UObject* a_wrapper, UE::UFunction* a_fn, void* a_params)
		{
			if (!g_open || a_wrapper != reflect::Get(g_ingredientEntryWrapper) || !a_params ||
			    a_fn->parmsSize != sizeof(UE::UObject*) ||
			    !pe::FunctionName(a_fn).ends_with("_OnListEntryGeneratedDynamic__DelegateSignature")) return;
			UE::UObject* row = nullptr;
			std::memcpy(&row, a_params, sizeof(row));
			if (!row || !reflect::IsLive(row)) return;
			// This is the wrapper's post-generation event, after the native entry has its item.
			// Only decorate the entry here. Replacing list items remains deferred (RMI-022).
			DecorateIngredientRow(row);
			ui::Call prepass(row, L"ForceLayoutPrepass");
			if (prepass) prepass.Run();
			g_nextLayout = {};
		}

		void SortNativeIngredientSource(UE::UObject* a_vm, UE::UFunction* a_fn, void* a_params)
		{
			if (!g_open || !g_sortRead || !a_params || a_vm != reflect::Get(g_ingredientSourceVM) ||
			pe::FunctionName(a_fn) != "GetItemsList") return;
			const auto& layout = Props();
			if (!layout.ok || layout.size != 0x40 || a_fn->parmsSize != sizeof(RawArray) ||
			    reflect::Offset(a_fn, "ReturnValue") != 0 || ui::SizeOf(a_fn, "ReturnValue") != sizeof(RawArray)) return;
			auto* element = ArrayStruct(a_fn, "ReturnValue");
			if (!element || pe::Utf8(element->GetFName().ToString()) != "LegacyIngredientItemProperties") return;
			const auto sourceOff = reflect::Offset(a_vm->GetClass(), "ItemsList");
			if (sourceOff < 0) return;
			const auto& result = *static_cast<const RawArray*>(a_params);
			const auto* stored = reflect::At<RawArray>(a_vm, sourceOff);
			if (!result.data || result.data == stored->data || result.num < 2 || result.num > 2000 || result.max < result.num) return;
			const auto stride = static_cast<std::size_t>(layout.size);
			std::vector<Ingredient> ingredients(static_cast<std::size_t>(result.num));
			for (std::size_t i = 0; i < ingredients.size(); ++i) {
				if (!ReadIngredient(result.data + i * stride, ingredients[i], true, g_sortState.key == SortKey::Colors)) return;
				if (g_sortState.key == SortKey::Colors)
					ingredients[i].markerPositions = IngredientMarkers(ingredients[i].knownEffects, g_markerNames);
			}
			const auto order = SortOrder(ingredients, g_sortState.key, g_sortState.descending);
			bool changed = false;
			for (std::size_t i = 0; i < order.size(); ++i) changed |= i != static_cast<std::size_t>(order[i]);
			if (!changed) return;
			// Permute whole structs in their existing allocation. Each owned member remains present exactly once.
			std::vector<std::uint8_t> ordered(order.size() * stride);
			for (std::size_t i = 0; i < order.size(); ++i)
				std::memcpy(ordered.data() + i * stride, result.data + static_cast<std::size_t>(order[i]) * stride, stride);
			std::memcpy(result.data, ordered.data(), ordered.size());
		}

		void WatchIngredientSource(UE::UObject* a_menu)
		{
			auto* vm = Field(a_menu, "VAlchemyMenuViewModel");
			if (vm && vm != reflect::Get(g_ingredientSourceVM) && pe::WatchPost(vm->GetClass(), &SortNativeIngredientSource))
				g_ingredientSourceVM = reflect::Hold(vm);
			auto* wrapper = Field(a_menu, "IngredientsListView");
			if (wrapper && wrapper != reflect::Get(g_ingredientEntryWrapper) &&
			    pe::WatchPost(wrapper->GetClass(), &OnIngredientEntryGenerated))
				g_ingredientEntryWrapper = reflect::Hold(wrapper);
		}

		void OnMenuEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void*)
		{
			if (!a_obj || !a_fn || !Enabled()) return;
			static std::unordered_map<UE::UFunction*, int> s_kind;   // 1 UpdateItemsList, 2 activation, 3 filter text, 4 destruct, 0 other
			auto it = s_kind.find(a_fn);
			if (it == s_kind.end()) {
				const auto n = pe::FunctionName(a_fn);
				const int  k = n == "UpdateItemsList"                                     ? 1 :
				               n == "BP_OnActivated" || n == "BP_OnDeactivated"           ? 2 :
				               n == "Update filter button text" || n == "CloseFilterWidget" ? 3 :
			               n == "Destruct"                                            ? 4 :
			               n == "InpActEvt_IA_Specific_AlchemyMenu_ClearAllFilters_Keyboard_K2Node_EnhancedInputActionEvent_2" ||
			               n == "InpActEvt_IA_Specific_AlchemyMenu_ClearAllFilters_K2Node_EnhancedInputActionEvent_7" ? 5 : 0;
				it = s_kind.emplace(a_fn, k).first;
			}
			if (!g_settingTab && g_filtersVisible.load()) {
				const auto event = pe::FunctionName(a_fn);
				if (event.find("NextTab") != std::string::npos) g_tabCycle = 1;
				else if (event.find("PreviousTab") != std::string::npos) g_tabCycle = -1;
			}
			if (it->second == 4) { g_menu = {}; return; }
			if (it->second == 5 && g_open) g_nativeClearRequested = true;
			if (reflect::Get(g_menu) != a_obj && reflect::IsLive(a_obj)) g_menu = reflect::Hold(a_obj);
			if (it->second == 1) g_state.ListBuilt();
			if (it->second == 1 && !g_inside && g_open) {
				g_searchList.nativeDirty = true;   // the game rebuilt the ingredient list: sort it on the next tick -
				// never inside the game's own call (RMI-022: rebuilding the list here released rows the game still used)
				g_dirtySort = true;
				g_dirtyCounts = true;
				g_nextLayout = {};
			}
			if (it->second == 2 || it->second == 3) g_nextLayout = {};
		}

		UE::UObject* Menu()
		{
			if (!g_menuClass) {
				static ULONGLONG s_next = 0;
				if (GetTickCount64() < s_next || !menus::AnyOpen()) return nullptr;
				s_next = GetTickCount64() + 500;
				g_menuClass = ui::Class(kMenuClass);
				if (!g_menuClass) return nullptr;
				pe::WatchPost(g_menuClass, &OnMenuEvent);
				for (auto* w : reflect::Instances(g_menuClass)) {   // already open when its class first loads
					if (Active(w)) { g_menu = reflect::Hold(w); break; }
				}
			}
			return reflect::Get(g_menu);
		}
	}

	using namespace detail;

	bool Open() { return g_openFlag.load(); }
	bool Naming() { return Open() && g_nameMode.load(); }
	UE::UObject* Widget() { return reflect::Get(g_menu); }
	bool SearchAvailable()
	{
		return Open() && !g_nameTyping.load() && !g_filtersVisible.load() && g_padFocus.load() < 0 && g_chipFocus.load() < 0;
	}
	bool SearchShown()
	{
		return Open() && !g_nameTyping.load() && !g_filtersVisible.load();
	}
	UE::UObject* FilterWidget() { return Field(Widget(), "Alchemy_Filters"); }
	bool FilterSearchAvailable()
	{
		return Open() && g_filtersVisible.load() && !g_nameTyping.load();
	}
	bool SearchRect(UE::UObject* a_canvas, double& a_x, double& a_y, double& a_w, double& a_h, bool a_filters)
	{
		auto* menu = Widget();
		auto* wrapper = a_filters ? Field(FilterWidget(), "ListView_Filters") : menu ? Field(menu, "IngredientsListView") : nullptr;
		if (a_filters) PadList(wrapper, kSearchSpace, g_paddedFilters);
		double lx = 0, ly = 0, lw = 0, lh = 0, cx = 0, cy = 0, cw = 0, ch = 0;
		if (!wrapper || !ui::Measure(wrapper, lx, ly, lw, lh) || !ui::Measure(a_canvas, cx, cy, cw, ch) || lw < 20) return false;
		const double scale = ui::ViewportScale();
		const double gap = kHeaderGap - kGapTrimPx / (scale > 0 ? scale : 1);
		a_x = lx - cx + lw * 0.1; // centered, 20% narrower overall (owner final styling)
		constexpr double kIngredientSearchLiftPx = 15;
		a_y = ly - cy - (a_filters ? 0 : gap + (kIngredientSearchLiftPx + kHeaderRuleDropPx) / (scale > 0 ? scale : 1)) - kSearchSpace;
		a_w = lw * 0.8;
		a_h = kSearchHeight;
		return true;
	}

	void Tick()
	{
		if (!Enabled() || !reflect::Ok()) return;
		auto*      menu = Menu();
		const bool open = menu && Active(menu);
		if (open != g_open) {
			g_open = open;
			g_openFlag = open;
			if (open) {
				g_filterTab = FilterTab::All;
				g_filterOpen = false;
				g_state.Opened(NowMs());
				g_countsOpened = true;
				if (!g_sortRead) {   // the INI default once; later openings keep the player's last sort
					const auto key = SortFromIni(mrconfig::Get().alchemy.defaultSort);
					g_sortState = { key, DefaultDescending(key) };
					g_sortRead = true;
				}
				g_padFocus = -1;
				g_hoverIcon = -1;
				g_dirtySort = g_dirtyCounts = true;
				g_nextLayout = {};
			} else {
				g_filtersVisible = false;
				g_tabRequest = -1;
				g_tabCycle = 0;
				g_state.Closed();
				g_nativeClearRequested = false;
				HideOwn();
				g_nameTyping = false;
			g_nameMode = false;
			g_nameKeyboardRequest = false;
			g_canName = false;
			g_padFocus = -1;
				g_dyDone.clear();
				g_chipFocus = -1;
			}
		}
		if (!open) return;
		WatchIngredientSource(menu);
		auto* nameBox = Field(menu, "Alchemy_NameBox");
		PotionNameTick(nameBox);
		ui::Call nameFocus(nameBox, L"HasFocusedDescendants");
		g_nameTyping = nameFocus && nameFocus.Run() && nameFocus.Get<bool>("ReturnValue");
		const auto query = searchcfg::Get().alchemy && searchcfg::Get().ownFilter ? search::GetText(menus::Menu::kAlchemy) : std::string();
		if (query != g_searchQuery) {
			g_searchQuery = query;
			// Query edits always start from the rooted native generation, never previous matches.
			logger::debug("alchemy search: filtering rooted native ingredient source");
			g_dirtySort = true;
		}
		auto* filters = Field(menu, "Alchemy_Filters");
		const bool filtersOpen = filters && Active(filters);
		g_filtersVisible = filtersOpen;
		const auto effectQuery = searchcfg::Get().alchemy && searchcfg::Get().ownFilter ? search::GetText(menus::Menu::kAlchemyFilters) : std::string();
		const bool effectQueryChanged = effectQuery != g_effectSearchQuery;
		g_effectSearchQuery = effectQuery;
		if (filtersOpen && (!g_filterOpen || g_effectSearchDirty)) {
			g_effectSearchDirty = false;
			ShowFilterTab(filters, g_filterTab);
		} else if (filtersOpen && effectQueryChanged) ApplyEffectSearch(filters);
		g_filterOpen = filtersOpen;
		if (filtersOpen) {
			if (const int requested = g_tabRequest.exchange(-1); requested >= 0 && requested < 3)
				ShowFilterTab(filters, static_cast<FilterTab>(requested));
			if (const int direction = g_tabCycle.exchange(0); direction != 0)
				ShowFilterTab(filters, alchemytabs::Cycle(g_filterTab, direction));
		}
		ApplySortInput();
		g_activeIcon = static_cast<int>(g_sortState.key);
		if (g_headerNow.exchange(false)) g_nextLayout = {};
		if (const int chip = g_chipClick.exchange(-1); chip >= 0 && chip < static_cast<int>(g_chipNames.size())) {
			const auto meaning = g_chipNames[static_cast<std::size_t>(chip)];
			const auto before = SelectedNames(menu).size();
			if (meaning.empty()) {
				RemoveEveryFilter(menu);
			} else if (meaning != "+") {
				RemoveFilter(menu, meaning);
			}
			const auto after = SelectedNames(menu).size();
			static bool s_said = false;
			if (after >= before && meaning != "+" && !s_said) {
				s_said = true;
				logger::warn("alchemy: removing a filter from the active-filter line changed nothing");
			}
			g_nextLayout = {};
		}
		if (!g_state.CanRead(NowMs(), kSettleMs)) {
			// RMI-038: the frame (mask strip, sort icons, tabs, footer hints, and the list gap the search bar sits in) reads no item
			// data, so it is drawn at once; retried every tick until the native widgets are measured. Data reads still wait.
			const auto early = Clock::now();
			if (early < g_nextLayout) return;
			Layout(menu, false);
			if (!g_framePlaced) return;
			g_earlyLayout = true;
			g_nextLayout = g_frameMoving ? early : early + std::chrono::milliseconds(100);   // sliding: every tick
			LayoutFilterTabs(menu);
			LayoutFooter(menu, filtersOpen, PadInput());
			return;
		}
		if (std::exchange(g_earlyLayout, false)) g_nextLayout = {};   // the list is built: full layout now, with filter names
		if (std::exchange(g_nativeClearRequested, false)) RemoveEveryFilter(menu);
		g_inside = true;
		if (g_dirtyCounts) ComputeCounts(menu);
		if (g_dirtySort) ApplySort(menu);
		g_inside = false;
		if (g_hoverNow.exchange(false)) {
			const int hovered = g_hoverChip.load(), padFocus = g_chipFocus.load();
			for (std::size_t i = 0; i < g_chipBacks.size(); ++i) StyleBack(i, static_cast<int>(i) == hovered || static_cast<int>(i) == padFocus);
		}
		const auto now = Clock::now();
		if (now < g_nextLayout) return;
		g_nextLayout = now + std::chrono::milliseconds(100);
		if (auto* filters = Field(menu, "Alchemy_Filters"); filters && Active(filters)) DecorateRows(filters, SelectedNames(menu));
		Layout(menu);
		DecorateIngredientRows(menu);
		LayoutFilterTabs(menu);
		const bool pad = PadInput();
		LayoutFooter(menu, filtersOpen, pad);
		if (g_frameMoving) g_nextLayout = now;   // still sliding in: place again on the next tick
		auto* name = Field(menu, "Alchemy_NameBox");
		ui::Call focus(name, L"HasFocusedDescendants");
		g_nameTyping = focus && focus.Run() && focus.Get<bool>("ReturnValue");
	}

}
