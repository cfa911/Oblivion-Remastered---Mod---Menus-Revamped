#pragma once
// Shared internals of the Alchemy menu: constants, types, state and helper declarations.
// Split from AlchemyMenu.cpp (2026-11-05); the definitions are in AlchemyData.cpp (reading the game's data),
// AlchemyLayout.cpp (our widgets and layout), AlchemyInput.cpp (keyboard, mouse, controller, potion naming) and
// AlchemyMenu.cpp (menu events and Tick).

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

#include "alchemy/AlchemyMenu.h"

#include "core/Config.h"
#include "search/Search.h"
#include "search/ListLifecycle.h"
#include "search/SearchConfig.h"
#include "keyboard/Keyboard.h"
#include "keyboard/KeyboardConfig.h"
#include "engine/TextUtil.h"
#include "core/Runtime.h"
#include "engine/Menus.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/Ui.h"
#include "engine/UiText.h"
#include "sorting/MenuSkin.h"
#include "sorting/rules/AlchemyRules.h"
#include "sorting/rules/AlchemyEffectMarkers.h"
#include "sorting/rules/ControllerPolicy.h"
#include "sorting/rules/AlchemyFilterTabs.h"

namespace alchemy
{
	namespace detail
	{
		using namespace alchemyrules;
		using Clock = std::chrono::steady_clock;
		inline const uistyle::Palette& UiPalette() { return uistyle::Get(mrconfig::Get().stylePreset); }

		constexpr const wchar_t* kMenuClass = L"/Game/UI/Modern/GameMenuLayer/Alchemy/WBP_ModernMenu_AlchemyMenu.WBP_ModernMenu_AlchemyMenu_C";
		// Only custom Alchemy controls are intercepted; native X/Y actions remain the game's.
		constexpr std::uint16_t  kPadUp = 0x0001, kPadDown = 0x0002, kPadLeft = 0x0004, kPadRight = 0x0008;
		constexpr std::uint16_t  kPadA = 0x1000, kPadB = 0x2000;
		constexpr std::uint16_t kPadName = 0x0040; // L3: native Name potion action
		constexpr std::uint16_t kPadLB = 0x0100, kPadRB = 0x0200;
		// IMP-MR-005 (owner 2026-11-04): the game's own sort header textures, loaded from the game at run time (KB
		// docs/inventory/internals.md "Native sort header icon textures"); index = SortKey (Name, Count, Weight, Value)
		constexpr int            kIcons = static_cast<int>(SortKey::kCount);
		constexpr const wchar_t* kTexPaths[] = {
			L"/Game/Art/UI/Icons/Icon_Stats/T_Property_1_sorting.T_Property_1_sorting",
			L"/Game/Art/UI/Icons/Icon_Stats/T_Property_1_cost.T_Property_1_cost",   // Count: placeholder (owner 2026-11-04)
			L"/Game/Art/UI/Icons/Icon_Stats/T_Property_1_Weight.T_Property_1_Weight",
			L"/Game/Art/UI/Icons/Icon_Stats/T_Property_1_MoneyV2.T_Property_1_MoneyV2",
			L"/Game/Art/UI/Common/Filter/T_Filter_Icon_Background_Small.T_Filter_Icon_Background_Small",
			L"/Game/Art/UI/Common/T_ArrowStat_indicationUp.T_ArrowStat_indicationUp",
			L"/Game/Art/UI/Common/T_ArrowStat_indicationDown.T_ArrowStat_indicationDown",
			L"/Game/Art/UI/Common/T_Line_V.T_Line_V",   // the Inventory's column line (ValueWithLine inv_entry_line)
			L"/Game/Art/UI/Icons/Icons_Inputs/Xbox/T_Icon_Xbox_Y.T_Icon_Xbox_Y",
			L"/Game/Art/UI/Icons/Icons_Inputs/Xbox/T_Icon_Xbox_A.T_Icon_Xbox_A",
			L"/Game/Art/UI/Icons/Icons_Inputs/Xbox/T_Icon_Xbox_B.T_Icon_Xbox_B",
			L"/Game/Art/UI/Icons/Icons_Inputs/Xbox/T_Icon_Xbox_LB.T_Icon_Xbox_LB",
			L"/Game/Art/UI/Icons/Icons_Inputs/Xbox/T_Icon_Xbox_RB.T_Icon_Xbox_RB",
			L"/Game/Art/UI/Icons/Icons_Inputs/Xbox/T_Icon_Xbox_DPad_Right.T_Icon_Xbox_DPad_Right",
			// the Count sort: the game's own category icon (owner 2026-11-11: no PNG copy of it; white base in MRDark_P)
			L"/Game/Art/UI/Icons/Icon_Category/T_Category_All_items.T_Category_All_items",
		};
		constexpr int            kTexBack = 4, kTexUp = 5, kTexDown = 6, kTexLine = 7, kTexPadY = 8;
		constexpr int            kTexPadA = 9, kTexPadB = 10, kTexPadLB = 11, kTexPadRB = 12, kTexPadRight = 13, kTexCategory = 14, kTexCount = 15;
		constexpr double         kHintGlyph = 40;   // own footer hint glyph (viewport units) when the native one is not measured
		constexpr double         kHintGap = 10;     // glyph to label
		constexpr double         kHintSpacing = 22; // between our hints and their left neighbour (owner screenshot b2d70c38)
		constexpr double         kSepWidth = 2;   // vertical dividers at icon height
		constexpr double         kIconSize = 34, kIconBack = 46, kIconStep = 52, kArrowSize = 14;   // viewport units
		constexpr double         kSearchSpace = 44, kSearchHeight = 36;
		constexpr double         kHeaderRuleDropPx = 3;
		constexpr double         kHeaderGap = 40;   // the list moves down this much; the icon line sits in the gap
		constexpr double         kFirstIcon = 25;   // first icon's centre from the list's left edge
		// owner adjustments 2026-11-04, in screen pixels (divided by the viewport scale): the gap 4 px smaller, the icon
		// line further from the rows, lightning and A-Z to the right
		constexpr double         kGapTrimPx = 4, kIconLiftPx = 6, kLeftShiftPx = 17;   // batches 2 and 3: +3 lift, +10 / +3 right
		constexpr double         kWeightCx = 120, kValueCx = 45;   // column centres from a row's right edge
		constexpr double         kMarkGap = 4;   // between the red x mark and the filter name
		constexpr double         kButtonPadX = 7, kButtonPadY = 2;
		constexpr double         kTitleAbove = 32;   // "Selected filters" sits this far above the first row
		constexpr int            kChipRows = 3;      // active-filter button rows (owner 2026-11-04)
		constexpr double         kRowGap = 5;        // between those rows

		struct RawArray { std::uint8_t* data; std::int32_t num; std::int32_t max; };

		struct Rect
		{
			double x = 0, y = 0, w = 0, h = 0;
			bool   ok = false;
			bool   Has(double a_x, double a_y) const { return ok && a_x >= x && a_x < x + w && a_y >= y && a_y < y + h; }
		};

		// ---- state (game thread unless atomic / under g_rectLock)
		inline UE::UClass*     g_menuClass = nullptr;
		inline UE::UClass*     g_entryClass = nullptr;
		inline reflect::Handle g_menu;
		inline bool            g_open = false;
		inline bool            g_inside = false;   // re-entrancy: our own list calls inside a watcher
		inline SortState       g_sortState;             // kept between openings; the INI default is read on the first one
		inline bool            g_sortRead = false;
		inline bool            g_dirtySort = true, g_dirtyCounts = true;
		inline OpenState       g_state;                 // RMI-021: no data read before this opening's list is built
		constexpr long long kSettleMs = 500;
		inline std::unordered_map<std::string, int> g_counts;
		inline bool            g_countsReady = false;
		inline bool            g_countsOpened = true;   // RMI-022: counts are read on opening ...
		inline int             g_countsNum = -1;        // ... and when the number of ingredients changes
		inline int             g_itemNames = 0;    // VModernAlchemyEffect name field: 0 not learned yet, 1 learned (g_nameOffset), -1 not found
		inline std::int32_t    g_nameOffset = -1;  // learned from a row: +0x38 (the run-4 guess) was not it (2026-11-04)
		inline Clock::time_point g_nextLayout{};
		inline bool            g_framePlaced = false;   // RMI-038: the last Layout found the native title row and placed the frame
		inline bool            g_earlyLayout = false;   // the frame was drawn before this opening's list was built
		inline double          g_frameX = 0;            // the native title row's x at the last Layout
		inline bool            g_frameMoving = false;   // it moved since the Layout before: the menu is sliding in (about 220 ms on reopening)
		inline std::unordered_map<std::string, std::pair<float, int>> g_formData;   // display name -> weight, value (-1 unknown)
		inline std::unordered_map<std::string, std::pair<float, int>> g_formMatch;  // the same by MatchKey (row text fallback)
		inline std::unordered_map<std::string, std::vector<std::string>> g_markerEffects;
		inline std::vector<std::string> g_markerNames; // only effects with a visible chip legend
		inline std::array<reflect::Handle, kMarkerLimit> g_chipCircles;
		inline alchemyrules::IngredientIndex g_ingredients;   // name key -> weight, value of every loaded IngredientItem
		inline bool g_ingredientsBuilt = false;

		inline reflect::Handle              g_titleBox;
		inline std::array<reflect::Handle, kIcons>    g_icons, g_iconBacks;   // the sort header (index = SortKey)
		inline reflect::Handle                         g_arrow;   // direction mark on the active icon
		inline std::array<reflect::Handle, 3>          g_seps;    // Count | Name, Weight | Value, Colors | Weight
		inline reflect::Handle                         g_rule;    // the line under the icons, across the list (owner 2026-11-04)
		inline int                                     g_arrowDir = -1;   // texture on g_arrow: -1 none, 0 up, 1 down
		inline std::array<reflect::Handle, kTexCount>  g_tex;
		inline reflect::Handle                         g_paddedList;   // the list wrapper whose slot got the header gap
		inline double                                  g_columnDy = 0;      // column labels' vertical correction (ColumnDy)
		inline bool                                    g_dyKnown = false;
		inline std::unordered_set<UE::UObject*>        g_dyDone;   // column labels already moved by g_columnDy
		inline std::vector<reflect::Handle> g_chips;   // created on demand; the entries' meaning is in g_chipNames
		inline std::vector<reflect::Handle> g_chipBacks;   // the button box behind each chip (an Image)
		inline std::vector<std::string>     g_chipText;    // the text each chip label was given (its width is then its own)
		inline std::vector<reflect::Handle> g_chipMarks;   // the red x mark before each filter name (owner 2026-11-04)
		inline reflect::Handle              g_more;        // the "+N" marker: its own label, never a chip's
		inline reflect::Handle              g_chipTitle;   // "Selected filters" over the buttons (owner 2026-11-04)
		inline std::unordered_map<std::string, double> g_textWidth;   // measured width of each text a chip has shown
		inline std::vector<int>             g_chipStyle;   // style applied to each box: -1 none, 0 normal, 1 hovered
		inline std::atomic<int>             g_hoverChip{ -1 };
		inline std::atomic<bool>            g_hoverNow{ false };   // the hovered button changed: restyle now, not at the next layout
		inline std::vector<std::string>     g_chipNames;   // effect display name per chip; "" = Clear all, "+" = overflow marker

		inline std::mutex        g_rectLock;
		inline std::array<Rect, kIcons> g_iconRects;
		inline std::vector<Rect> g_chipRects;
		inline std::atomic<int> g_sortDirection{ -1 }; // focused icon * 2 + descending; explicit Up/Down request
		inline pickerpad::State g_sortNav; // shared deadzone, repeat and stick suppression
		inline std::atomic<int>  g_iconClick{ -1 };    // a header icon clicked (mouse or A)
		inline std::atomic<int>  g_hoverIcon{ -1 };
		inline std::atomic<int>  g_padFocus{ -1 };     // controller focus on the header: icon index, -1 = not in the header
		inline std::atomic<int>  g_activeIcon{ 0 };
		inline std::atomic<bool> g_headerNow{ false }; // hover or focus changed: lay out now
		inline std::uint16_t     g_padSwallow = 0;     // buttons the header used, hidden from the game until released
		inline std::atomic<int>  g_chipFocus{ -1 };     // RMI-027: controller focus on the Selected Filters buttons (-1 none)
		inline std::atomic<int>  g_chipCount{ 0 };      // Selected Filters buttons shown (Layout)
		inline reflect::Handle   g_inputSubsystem;      // the game's CommonInputSubsystem (pad or keyboard)
		inline std::atomic<int>  g_chipClick{ -1 };
		inline std::atomic<bool> g_openFlag{ false };
		inline std::atomic<bool> g_nameTyping{ false };
		inline std::atomic<bool> g_nameMode{ false }, g_nameKeyboardRequest{ false }, g_canName{ false };
		inline std::string g_nameApplied;
		inline std::string g_searchQuery;
		inline revamped::ListLifecycle<UE::UObject*> g_searchList;
		inline reflect::Handle g_searchCache;
		inline reflect::Handle g_effectSearchCache, g_paddedFilters;
		inline std::vector<UE::UObject*> g_effectSearchItems;
		inline std::string g_effectSearchQuery;
		inline bool g_effectSearchReady = false, g_effectSearchDirty = false;
		inline bool g_nativeClearRequested = false;
		inline bool              g_swallowUp = false;
		inline std::uint16_t     g_lastButtons = 0;

		// ---- reading ingredients
		struct PropLayout
		{
			std::int32_t size = -1, name = -1, quantity = -1, effects = -1;
			std::int32_t effectSize = -1, effectUnknown = -1, effectName = -1;
			bool         unknownInverted = false;   // the flag is bIsKnown (true = known)
			bool         ok = false;
		};

		// the filter item's effect name: the FText at +0x30 (VModernAlchemyEffect has no reflected fields; proven by matching
		// the rows 2026-11-04). Only that one field is read - never a scan (RMI-022: a scan called ToString on garbage).
		constexpr std::int32_t kEffectNameCandidate = 0x30;

		bool PadInput();

		using FilterTab = alchemytabs::Tab;
		inline FilterTab g_filterTab = FilterTab::All;
		inline bool g_filterOpen = false, g_settingTab = false;
		inline bool g_menuInked = false;   // Dark: the menu tree's brown text inked light once per opening (menuskin::InkTree)
		inline std::atomic<int> g_tabRequest{ -1 }, g_tabCycle{ 0 };
		inline std::atomic<bool> g_filtersVisible{ false };
		inline std::array<reflect::Handle, 3> g_tabLabels, g_tabLeft, g_tabRight, g_tabDashLeft, g_tabDashRight, g_tabUpperLeft, g_tabUpperRight, g_tabLowerLeft, g_tabLowerRight;
		inline reflect::Handle g_tabOwner;
		inline std::atomic<int> g_hoverTab{ -1 };
		inline std::array<Rect, 3> g_tabRects{};

		inline UE::UClass* g_ingredientRowClass = nullptr;

		// one hint: [glyph (with the hold rim)] label - built like the native entry beside it
		// the controller bar in the owner's order (2026-11-04); the game's bar cannot reorder or insert, so on a pad all six
		// are ours and the native entries are hidden. mirror: the label is copied from the game's own press entry (0 = its
		// first, A Add to Potion; 1 = its second, B Back) so it stays localised and follows the game's wording
		struct Hint { int glyph; bool hold; const char* label; int mirror; };
		constexpr Hint kHints[] = {
			{ kTexPadA, false, nullptr, 0 },
			{ kTexPadY, false, "Filters", -1 },
			{ kTexPadY, true, "Clear All Filters", -1 },
			{ kTexPadRB, false, "Sort", -1 },
			{ kTexPadLB, false, "Selected Filters", -1 },
			{ kTexPadB, false, nullptr, 1 },
			{ kTexPadRight, true, "Search", -1 },
		};

		// Order the native getter's independent result before the game builds row widgets.
		inline reflect::Handle g_ingredientSourceVM;
		inline reflect::Handle g_ingredientEntryWrapper;

		// ---- helper functions (defined in the Alchemy*.cpp files)
		long long       NowMs();
		bool Enabled();
		UE::UObject* Field(UE::UObject* a_obj, std::string_view a_name);
		bool Flag(UE::UObject* a_obj, std::string_view a_name);
		UE::UObject* InnerList(UE::UObject* a_wrapper);
		std::vector<UE::UObject*> ListItems(UE::UObject* a_list);
		std::vector<UE::UObject*> DisplayedRows(UE::UObject* a_list);
		void SetItems(UE::UObject* a_list, const std::vector<UE::UObject*>& a_items);
		std::wstring FindFunction(UE::UClass* a_class, std::initializer_list<std::string_view> a_parts);
		bool ReadPointer(const std::uint8_t* a_address, const std::uint8_t** a_out);
		bool CopyName(const UE::FName* a_name, std::string* a_out);
		bool NameGuarded(const UE::FName* a_name, std::string* a_out);
		UE::UStruct* ArrayStruct(UE::UStruct* a_owner, std::string_view a_array);
		const PropLayout& Props();
		bool CopyText(const UE::FText* a_text, std::string* a_out);
		bool TextGuarded(const UE::FText* a_text, std::string* a_out);
		bool ReadQword(unsigned long long a_address, unsigned long long* a_out);
		std::pair<unsigned long long, unsigned long long> GameImage();
		bool SafeText(const UE::FText* a_text, std::string& a_out);
		void AddIngredientForm(RE::TESForm* a_form);
		bool WalkForms();
		bool WalkFormsGuarded();
		void BuildIngredientIndex();
		std::pair<float, int> FormData(const std::string& a_name, const UE::FText& a_text);
		bool ReadIngredient(const std::uint8_t* a_props, Ingredient& a_out, bool a_formData, bool a_effects = true);
		bool LooksLikeText(const std::uint8_t* a_p);
		bool EffectItemName(UE::UObject* a_item, std::string& a_out);
		int CountOf(const std::string& a_display);
		void ComputeCounts(UE::UObject* a_menu);
		UE::UObject* CountLabel(UE::UObject* a_row, UE::UObject* a_text);
		void DecorateRow(UE::UObject* a_row);
		std::vector<std::pair<int, std::string>> ItemTexts(UE::UObject* a_item);
		void OnEntryEvent(UE::UObject* a_row, UE::UFunction* a_fn, void* a_params);
		void RestoreSelected(UE::UObject* a_row, const std::vector<std::string>& a_active);
		void ClearStaleFilterHover(const std::vector<UE::UObject*>& a_rows);
		void DecorateRows(UE::UObject* a_filters, const std::vector<std::string>& a_active);
		std::string ReadPotionName(UE::UObject* a_name);
		bool NameTextCall(UE::UObject* a_name, const wchar_t* a_function, const char* a_parameter, const std::string& a_value, bool a_commit = false);
		void PotionNameTick(UE::UObject* a_name);
		bool RootSearchSource(UE::UObject* a_menu, const std::vector<UE::UObject*>& a_items, reflect::Handle& a_cache, const wchar_t* a_name);
		bool RootSearchItems(UE::UObject* a_menu, const std::vector<UE::UObject*>& a_items);
		bool MatchesQuery(const Ingredient& a_item, const std::string& a_query);
		void ApplySort(UE::UObject* a_menu);
		bool LooksLikeFString(const std::uint8_t* a_elem);
		std::vector<std::string> SelectedNames(UE::UObject* a_menu);
		UE::UObject* FindFilterItem(UE::UObject* a_list, const std::string& a_key);
		bool ClickTab(UE::UObject* a_filters, bool a_positive);
		void RemoveFilter(UE::UObject* a_menu, const std::string& a_display);
		void RemoveEveryFilter(UE::UObject* a_menu);
		void ApplyEffectSearch(UE::UObject* a_filters);
		void ShowFilterTab(UE::UObject* a_filters, FilterTab a_tab);
		UE::UObject* MakeLabel(UE::UObject* a_canvas, const wchar_t* a_name, const float* a_ink, double a_size);
		UE::UObject* SlotOf(UE::UObject* a_widget);
		void Place(UE::UObject* a_label, double a_x, double a_y, double a_alignX);
		std::array<double, 2> DesiredSize(UE::UObject* a_label);
		double DesiredWidth(UE::UObject* a_label);
		UE::UObject* StripTexture();
		UE::UObject* MakeBack(UE::UObject* a_canvas, std::size_t a_index);
		void StyleBack(std::size_t a_index, bool a_hovered);
		double MarkWidth();
		Rect PixelRect(UE::UObject* a_widget, double a_scale);
		void HideOwn();
		UE::UObject* MakeTabOrnament(UE::UObject* a_canvas, UE::UObject* a_source, const wchar_t* a_name);
		void CopyIngredientsFont(UE::UObject* a_label, UE::UObject* a_title);
		void CopyTabFont(UE::UObject* a_label, UE::UObject* a_original);
		void LayoutFilterTabs(UE::UObject* a_menu);
		UE::UObject* NativeTexture(int a_index);
		void SetTexture(UE::UObject* a_image, UE::UObject* a_texture);
		UE::UObject* MakeImage(UE::UObject* a_canvas, const wchar_t* a_name, UE::UObject* a_texture, int a_z, double a_size);
		void ResetHeader();
		void PadList(UE::UObject* a_wrapper, double a_gap, reflect::Handle& a_padded = g_paddedList);
		std::array<Rect, kIcons> LayoutHeader(UE::UObject* a_canvas, UE::UObject* a_menu, double a_scale);
		UE::UObject* RowNameText(UE::UObject* a_row);
		UE::UObject* ColumnLabel(UE::UObject* a_row, UE::UObject* a_text, const wchar_t* a_name, double a_fromRight);
		UE::UObject* EffectCircle(UE::UObject* a_canvas, UE::UObject* a_owner, const wchar_t* a_name);
		void DecorateIngredientMarkers(UE::UObject* a_row, const std::string& a_name);
		void DecorateIngredientRow(UE::UObject* a_row);
		void OnIngredientItemBound(UE::UObject* a_row, UE::UFunction* a_fn, void*);
		void DecorateIngredientRows(UE::UObject* a_menu);
		void Layout(UE::UObject* a_menu, bool a_readNames = true);
		bool PadInput();
		std::vector<UE::UObject*> PanelChildren(UE::UObject* a_panel);
		std::string ObjName(UE::UObject* a_o);
		void CopyLook(UE::UObject* a_to, UE::UObject* a_from);
		UE::UObject* NewWidget(UE::UObject* a_outer, const wchar_t* a_class, const std::wstring& a_name);
		UE::UObject* AddTo(UE::UObject* a_panel, const wchar_t* a_fn, UE::UObject* a_child);
		void FillSlot(UE::UObject* a_slot);
		double HoldTime(UE::UObject* a_entry);
		double GlyphSide(UE::UObject* a_entry);
		UE::UObject* MakeHint(UE::UObject* a_box, int a_index, UE::UObject* a_native, UE::UObject* a_holdEntry, UE::UObject* a_pressEntry);
		UE::UObject* OwnBar(UE::UObject* a_footer, UE::UObject* a_box);
		void LayoutFooter(UE::UObject* a_menu, bool a_filtersOpen, bool a_pad);
		int MoveChipFocus(int a_index, pickerpad::Direction a_direction);
		void ApplySortInput();
		bool Active(UE::UObject* a_menu);
		void OnIngredientEntryGenerated(UE::UObject* a_wrapper, UE::UFunction* a_fn, void* a_params);
		void SortNativeIngredientSource(UE::UObject* a_vm, UE::UFunction* a_fn, void* a_params);
		void WatchIngredientSource(UE::UObject* a_menu);
		void OnMenuEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void*);
		UE::UObject* Menu();
	}
}
