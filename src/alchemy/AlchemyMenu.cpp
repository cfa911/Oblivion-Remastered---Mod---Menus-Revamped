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
#include "sorting/rules/AlchemyRules.h"
#include "sorting/rules/AlchemyEffectMarkers.h"
#include "sorting/rules/ControllerPolicy.h"
#include "sorting/rules/AlchemyFilterTabs.h"

namespace alchemy
{
	namespace
	{
		using namespace alchemyrules;
		using Clock = std::chrono::steady_clock;

		constexpr const wchar_t* kMenuClass = L"/Game/UI/Modern/GameMenuLayer/Alchemy/WBP_ModernMenu_AlchemyMenu.WBP_ModernMenu_AlchemyMenu_C";
		// Only custom Alchemy controls are intercepted; native X/Y actions remain the game's.
		constexpr std::uint16_t  kPadUp = 0x0001, kPadDown = 0x0002, kPadLeft = 0x0004, kPadRight = 0x0008;
		constexpr std::uint16_t  kPadA = 0x1000, kPadB = 0x2000;
		constexpr std::uint16_t kPadName = 0x0040; // L3: native Name potion action
		constexpr std::uint16_t kPadLB = 0x0100, kPadRB = 0x0200;
		// IMP-MR-005 (owner 2026-11-04): the game's own sort header textures, loaded from the game at run time (KB
		// docs/inventory/internals.md "Native sort header icon textures"); index = SortKey (Name, Count, Weight, Value)
		constexpr int            kIcons = 4;
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
		};
		constexpr int            kTexBack = 4, kTexUp = 5, kTexDown = 6, kTexLine = 7, kTexPadY = 8;
		constexpr int            kTexPadA = 9, kTexPadB = 10, kTexPadLB = 11, kTexPadRB = 12, kTexPadRight = 13, kTexCount = 14;
		constexpr double         kHintGlyph = 40;   // own footer hint glyph (viewport units) when the native one is not measured
		constexpr double         kHintGap = 10;     // glyph to label
		constexpr double         kHintSpacing = 22; // between our hints and their left neighbour (owner screenshot b2d70c38)
		constexpr double         kSepWidth = 2;   // separators lightning|A-Z and weight|value, icon height
		constexpr double         kIconSize = 34, kIconBack = 46, kIconStep = 52, kArrowSize = 14;   // viewport units
		constexpr float          kActiveTint[4] = { 0.55f, 0.35f, 0.20f, 1.0f };    // active sort: brown; hover/focus unchanged (owner 2026-11-05)
		constexpr float          kFocusTint[4] = { 1.0f, 1.0f, 1.0f, 0.55f };    // hovered / controller focus: fainter
		constexpr double         kSearchSpace = 44, kSearchHeight = 36;
		constexpr double         kHeaderGap = 40;   // the list moves down this much; the icon line sits in the gap
		constexpr double         kFirstIcon = 25;   // first icon's centre from the list's left edge
		// owner adjustments 2026-11-04, in screen pixels (divided by the viewport scale): the gap 4 px smaller, the icon
		// line further from the rows, lightning and A-Z to the right
		constexpr double         kGapTrimPx = 4, kIconLiftPx = 6, kLeftShiftPx = 17;   // batches 2 and 3: +3 lift, +10 / +3 right
		constexpr double         kWeightCx = 120, kValueCx = 45;   // column centres from a row's right edge
		constexpr float          kSortInk[4] = { 0.035f, 0.025f, 0.015f, 1.0f };   // dark ink (list text)
		constexpr float          kChipInk[4] = { 0.30f, 0.012f, 0.008f, 1.0f };    // the Filters underline red
		constexpr float          kZeroOpacity = 0.4f;
		// active-filter buttons: parchment-dark fill, lighter while hovered, outline in the chip red
		constexpr float          kButtonFill[4] = { 0.30f, 0.24f, 0.16f, 0.30f };
		constexpr float          kButtonHover[4] = { 0.62f, 0.50f, 0.30f, 0.65f };
		constexpr float          kButtonLine[4] = { 0.30f, 0.012f, 0.008f, 1.0f };
		constexpr float          kTitleInk[4] = { 0.0723f, 0.0382f, 0.0284f, 1.0f };   // #4c372f (sRGB) as linear, owner 2026-11-04
		constexpr float          kButtonLineDark[4] = { 0.0f, 0.0f, 0.0f, 1.0f };   // filter buttons: black rim (owner 2026-11-04)
		constexpr double         kMarkGap = 4;   // between the red x mark and the filter name
		constexpr double         kButtonPadX = 7, kButtonPadY = 2;
		constexpr double         kChipText = 13;     // button text size (17 -> 15 -> 13, owner 2026-11-04)
		constexpr double         kTitleAbove = 32;   // "Selected filters" sits this far above the first row
		constexpr double         kChipsDy = -16;     // the title + rows block, up into the strip top (owner 2026-11-04)
		constexpr int            kChipRows = 2;      // active-filter button rows (owner 2026-11-04)
		constexpr double         kRowGap = 5;        // between those rows
		// Owner's updated Better Mask PSD (2026-11-04 18:50): 1090x263, at 100,0 on the 2559x1439 canvas.
		// Panel background measured 60.8,93.7,845.5 viewport units; mask sits at 75, 0 with width 817.5.
		constexpr const wchar_t* kStripFile = L"Alchemy-TopParchment.png";
		constexpr double         kStripAspect = 263.0 / 1090.0, kStripDx = 14.2, kStripDy = -93.7, kStripDw = -28.0;

		struct RawArray { std::uint8_t* data; std::int32_t num; std::int32_t max; };

		struct Rect
		{
			double x = 0, y = 0, w = 0, h = 0;
			bool   ok = false;
			bool   Has(double a_x, double a_y) const { return ok && a_x >= x && a_x < x + w && a_y >= y && a_y < y + h; }
		};

		// ---- state (game thread unless atomic / under g_rectLock)
		UE::UClass*     g_menuClass = nullptr;
		UE::UClass*     g_entryClass = nullptr;
		reflect::Handle g_menu;
		bool            g_open = false;
		bool            g_inside = false;   // re-entrancy: our own list calls inside a watcher
		SortState       g_sortState;             // kept between openings; the INI default is read on the first one
		bool            g_sortRead = false;
		bool            g_dirtySort = true, g_dirtyCounts = true;
		OpenState       g_state;                 // RMI-021: no data read before this opening's list is built
		constexpr long long kSettleMs = 500;
		long long       NowMs() { return static_cast<long long>(GetTickCount64()); }
		std::unordered_map<std::string, int> g_counts;
		bool            g_countsReady = false;
		bool            g_countsOpened = true;   // RMI-022: counts are read on opening ...
		int             g_countsNum = -1;        // ... and when the number of ingredients changes
		int             g_itemNames = 0;    // VModernAlchemyEffect name field: 0 not learned yet, 1 learned (g_nameOffset), -1 not found
		std::int32_t    g_nameOffset = -1;  // learned from a row: +0x38 (the run-4 guess) was not it (2026-11-04)
		Clock::time_point g_nextLayout{};
		bool            g_framePlaced = false;   // RMI-038: the last Layout found the native title row and placed the frame
		bool            g_earlyLayout = false;   // the frame was drawn before this opening's list was built
		double          g_frameX = 0;            // the native title row's x at the last Layout
		bool            g_frameMoving = false;   // it moved since the Layout before: the menu is sliding in (about 220 ms on reopening)
		std::unordered_map<std::string, std::pair<float, int>> g_formData;   // display name -> weight, value (-1 unknown)
		std::unordered_map<std::string, std::pair<float, int>> g_formMatch;  // the same by MatchKey (row text fallback)
		std::unordered_map<std::string, std::vector<std::string>> g_markerEffects;
		std::vector<std::string> g_markerNames; // only effects with a visible chip legend
		std::array<reflect::Handle, kMarkerLimit> g_chipCircles;
		alchemyrules::IngredientIndex g_ingredients;   // name key -> weight, value of every loaded IngredientItem
		bool g_ingredientsBuilt = false;

		reflect::Handle              g_titleBox, g_strip, g_stripTexture;
		std::array<reflect::Handle, kIcons>    g_icons, g_iconBacks;   // the sort header (index = SortKey)
		reflect::Handle                         g_arrow;   // direction mark on the active icon
		std::array<reflect::Handle, 2>          g_seps;    // lightning | A-Z and weight | value (owner 2026-11-04)
		reflect::Handle                         g_rule;    // the line under the icons, across the list (owner 2026-11-04)
		int                                     g_arrowDir = -1;   // texture on g_arrow: -1 none, 0 up, 1 down
		std::array<reflect::Handle, kTexCount>  g_tex;
		reflect::Handle                         g_paddedList;   // the list wrapper whose slot got the header gap
		double                                  g_columnDy = 0;      // column labels' vertical correction (ColumnDy)
		bool                                    g_dyKnown = false;
		std::unordered_set<UE::UObject*>        g_dyDone;   // column labels already moved by g_columnDy
		int                          g_stripGreyed = -1;   // tint applied to the strip: -1 none yet, 0 full, 1 greyed
		std::vector<reflect::Handle> g_chips;   // created on demand; the entries' meaning is in g_chipNames
		std::vector<reflect::Handle> g_chipBacks;   // the button box behind each chip (an Image)
		std::vector<std::string>     g_chipText;    // the text each chip label was given (its width is then its own)
		std::vector<reflect::Handle> g_chipMarks;   // the red x mark before each filter name (owner 2026-11-04)
		reflect::Handle              g_more;        // the "+N" marker: its own label, never a chip's
		reflect::Handle              g_chipTitle;   // "Selected filters" over the buttons (owner 2026-11-04)
		std::unordered_map<std::string, double> g_textWidth;   // measured width of each text a chip has shown
		std::vector<int>             g_chipStyle;   // style applied to each box: -1 none, 0 normal, 1 hovered
		std::atomic<int>             g_hoverChip{ -1 };
		std::atomic<bool>            g_hoverNow{ false };   // the hovered button changed: restyle now, not at the next layout
		std::vector<std::string>     g_chipNames;   // effect display name per chip; "" = Clear all, "+" = overflow marker

		std::mutex        g_rectLock;
		std::array<Rect, kIcons> g_iconRects;
		std::vector<Rect> g_chipRects;
		std::atomic<int> g_sortDirection{ -1 }; // focused icon * 2 + descending; explicit Up/Down request
		pickerpad::State g_sortNav; // shared deadzone, repeat and stick suppression
		std::atomic<int>  g_iconClick{ -1 };    // a header icon clicked (mouse or A)
		std::atomic<int>  g_hoverIcon{ -1 };
		std::atomic<int>  g_padFocus{ -1 };     // controller focus on the header: icon index, -1 = not in the header
		std::atomic<int>  g_activeIcon{ 0 };
		std::atomic<bool> g_headerNow{ false }; // hover or focus changed: lay out now
		std::uint16_t     g_padSwallow = 0;     // buttons the header used, hidden from the game until released
		std::atomic<int>  g_chipFocus{ -1 };     // RMI-027: controller focus on the Selected Filters buttons (-1 none)
		std::atomic<int>  g_chipCount{ 0 };      // Selected Filters buttons shown (Layout)
		reflect::Handle   g_inputSubsystem;      // the game's CommonInputSubsystem (pad or keyboard)
		std::atomic<int>  g_chipClick{ -1 };
		std::atomic<bool> g_openFlag{ false };
		std::atomic<bool> g_nameTyping{ false };
		std::atomic<bool> g_nameMode{ false }, g_nameKeyboardRequest{ false }, g_canName{ false };
		std::string g_nameApplied;
		std::string g_searchQuery;
		revamped::ListLifecycle<UE::UObject*> g_searchList;
		reflect::Handle g_searchCache;
		reflect::Handle g_effectSearchCache, g_paddedFilters;
		std::vector<UE::UObject*> g_effectSearchItems;
		std::string g_effectSearchQuery;
		bool g_effectSearchReady = false, g_effectSearchDirty = false;
		bool g_nativeClearRequested = false;
		bool              g_swallowUp = false;
		std::uint16_t     g_lastButtons = 0;

		bool Enabled() { return mrconfig::Get().alchemy.enabled; }

		// ---- reflection helpers
		UE::UObject* Field(UE::UObject* a_obj, std::string_view a_name)
		{
			if (!a_obj || !a_obj->GetClass()) return nullptr;
			const auto off = reflect::Offset(a_obj->GetClass(), a_name);
			if (off < 0) return nullptr;
			auto* p = *reflect::At<UE::UObject*>(a_obj, off);
			return p && reflect::IsLive(p) ? p : nullptr;
		}

		bool Flag(UE::UObject* a_obj, std::string_view a_name)
		{
			const auto off = a_obj && a_obj->GetClass() ? reflect::Offset(a_obj->GetClass(), a_name) : -1;
			return off >= 0 && *reflect::At<bool>(a_obj, off);
		}

		UE::UObject* InnerList(UE::UObject* a_wrapper) { return Field(a_wrapper, "ListView"); }

		std::vector<UE::UObject*> ListItems(UE::UObject* a_list)
		{
			std::vector<UE::UObject*> out;
			ui::Call g(a_list, L"GetListItems");
			void*    ret = g ? g.At("ReturnValue") : nullptr;
			if (!ret || !g.Run()) return out;
			auto*      data = *reinterpret_cast<UE::UObject***>(ret);
			const auto num = *reinterpret_cast<std::int32_t*>(static_cast<std::uint8_t*>(ret) + 8);
			for (std::int32_t i = 0; data && i < num && i < 4000; ++i) {
				if (data[i]) out.push_back(data[i]);
			}
			if (data) UE::FMemory::Free(data);
			return out;
		}

		std::vector<UE::UObject*> DisplayedRows(UE::UObject* a_list)
		{
			std::vector<UE::UObject*> out;
			ui::Call shown(a_list, L"GetDisplayedEntryWidgets");
			const auto* rows = shown && shown.Run() ? static_cast<const RawArray*>(shown.At("ReturnValue")) : nullptr;
			auto* const* data = rows ? reinterpret_cast<UE::UObject* const*>(rows->data) : nullptr;
			for (std::int32_t i = 0; data && i < rows->num && i < 64; ++i) {
				if (data[i]) out.push_back(data[i]);
			}
			return out;
		}

		void SetItems(UE::UObject* a_list, const std::vector<UE::UObject*>& a_items)
		{
			// UListView copies this const input during the call. Avoid publishing a transient empty list.
			ui::Call batch(a_list, L"BP_SetListItems");
			const auto count = static_cast<std::int32_t>(a_items.size());
			const RawArray items{ reinterpret_cast<std::uint8_t*>(const_cast<UE::UObject**>(a_items.data())), count, count };
			if (batch && batch.Size("InListItems") == sizeof(items) && batch.Set("InListItems", items) && batch.Run()) return;
			ui::Call clear(a_list, L"ClearListItems");
			if (!clear) return;
			clear.Run();
			for (auto* item : a_items) {
				ui::Call add(a_list, L"AddItem");
				add.Set("Item", item);
				add.Run();
			}
			ui::Call refresh(a_list, L"RequestRefresh");
			if (refresh) refresh.Run();
		}

		// a function of a_class whose name holds every part (bound-event names carry generated numbers)
		std::wstring FindFunction(UE::UClass* a_class, std::initializer_list<std::string_view> a_parts)
		{
			for (auto* s = static_cast<UE::UStruct*>(a_class); s; s = s->superStruct) {
				int guard = 0;
				for (auto* f = s->children.Get(); f && ++guard < 2000; f = f->next) {
					const auto name = pe::Utf8(f->GetFName().ToString());
					bool all = true;
					for (auto part : a_parts) all &= name.find(part) != std::string::npos;
					if (all) return std::wstring(name.begin(), name.end());
				}
				break;   // the Blueprint class itself declares its bound events
			}
			return {};
		}

		// ---- reading ingredients
		struct Layout
		{
			std::int32_t size = -1, name = -1, quantity = -1, effects = -1;
			std::int32_t effectSize = -1, effectUnknown = -1, effectName = -1;
			bool         unknownInverted = false;   // the flag is bIsKnown (true = known)
			bool         ok = false;
		};

		bool ReadPointer(const std::uint8_t* a_address, const std::uint8_t** a_out)
		{
			__try {
				*a_out = *reinterpret_cast<const std::uint8_t* const*>(a_address);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		bool CopyName(const UE::FName* a_name, std::string* a_out) { *a_out = pe::Utf8(a_name->ToString()); return true; }

		bool NameGuarded(const UE::FName* a_name, std::string* a_out)
		{
			__try {
				return CopyName(a_name, a_out);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		// RMI-023: the struct an array property holds (FArrayProperty::Inner -> FStructProperty::Struct), not an assumed
		// one. The inner property carries the array's own name; the struct must be a live ScriptStruct. Offsets of the two
		// links are tried in the range UE 5.x uses and proven by those checks.
		UE::UStruct* ArrayStruct(UE::UStruct* a_owner, std::string_view a_array)
		{
			const auto* field = reflect::FindField(a_owner, a_array);
			if (!field) return nullptr;
			static auto* scriptStruct = ui::Class(L"/Script/CoreUObject.ScriptStruct");
			for (std::ptrdiff_t innerOff : { 0x70, 0x78, 0x80 }) {
				const std::uint8_t* inner = nullptr;
				if (!ReadPointer(field + innerOff, &inner) || !inner || reinterpret_cast<std::uintptr_t>(inner) < 0x10000) continue;
				std::string innerName;
				if (!NameGuarded(reinterpret_cast<const UE::FName*>(inner + 0x20), &innerName) || innerName != a_array) continue;
				for (std::ptrdiff_t structOff : { 0x70, 0x78, 0x80 }) {
					const std::uint8_t* candidate = nullptr;
					if (!ReadPointer(inner + structOff, &candidate) || !candidate) continue;
					auto* obj = reinterpret_cast<UE::UObject*>(const_cast<std::uint8_t*>(candidate));
					if (reflect::IsLive(obj) && obj->GetClass() == scriptStruct) {
						return reinterpret_cast<UE::UStruct*>(obj);
					}
				}
			}
			return nullptr;
		}

		const Layout& Props()
		{
			static Layout l;
			static bool   tried = false;
			if (tried) return l;
			tried = true;
			auto* props = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Altar.LegacyIngredientItemProperties"));
			auto* effect = props ? ArrayStruct(props, "Effects") : nullptr;   // RMI-023: the struct the array really holds
			if (!props || !effect) {
				logger::warn("alchemy: ingredient structs not found; sort and counts are off");
				return l;
			}
			l.size = props->propertiesSize;
			l.name = reflect::Offset(props, "NameText");
			l.quantity = reflect::Offset(props, "Quantity");
			l.effects = reflect::Offset(props, "Effects");
			l.effectSize = effect->propertiesSize;
			std::vector<FieldInfo> fields;
			std::string            list;
			for (auto& [name, offset] : reflect::Fields(effect)) {
				fields.push_back({ name, offset, ui::SizeOf(effect, name) });
				list += name + "+" + std::to_string(offset) + "/" + std::to_string(fields.back().size) + " ";
			}
			const auto [flag, inverted] = UnknownFlag(fields);
			l.effectUnknown = flag;   // -1 when the struct has no such flag
			l.unknownInverted = inverted;
			l.effectName = TextField(fields, static_cast<int>(sizeof(UE::FText)));
			l.ok = l.size > 0 && l.name >= 0 && l.quantity >= 0 && l.effects >= 0 && l.effectSize > 0 && l.effectName >= 0 &&
			       l.effectName + static_cast<std::int32_t>(sizeof(UE::FText)) <= l.effectSize &&
			       ui::SizeOf(props, "NameText") == static_cast<std::int32_t>(sizeof(UE::FText));
			if (!l.ok) logger::warn("alchemy: ingredient struct layout unexpected (size {:X}, effect size {:X}); sort and counts are off", l.size, l.effectSize);
			return l;
		}

		// RMI-021: an FText read from game data - plausible pointers first, then the engine's ToString under a fault guard
		bool CopyText(const UE::FText* a_text, std::string* a_out) { *a_out = reflect::Text(*a_text); return true; }

		bool TextGuarded(const UE::FText* a_text, std::string* a_out)
		{
			__try {
				return CopyText(a_text, a_out);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		bool ReadQword(unsigned long long a_address, unsigned long long* a_out)
		{
			__try {
				*a_out = *reinterpret_cast<const unsigned long long*>(a_address);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		// the game executable's image (vtables of live engine objects lie in it)
		std::pair<unsigned long long, unsigned long long> GameImage()
		{
			static std::pair<unsigned long long, unsigned long long> s_image{ 0, 0 };
			if (!s_image.first) {
				const auto base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
				const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
				const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
				s_image = { reinterpret_cast<unsigned long long>(base), nt->OptionalHeader.SizeOfImage };
			}
			return s_image;
		}

		bool SafeText(const UE::FText* a_text, std::string& a_out)
		{
			const auto* raw = reinterpret_cast<const unsigned long long*>(a_text);
			if (!a_text || !PlausibleText(raw[0], raw[1])) return false;
			unsigned long long vtable = 0;   // RMI-022: only a live text object - its vtable lies in the game image
			const auto [imageBase, imageSize] = GameImage();
			if (!ReadQword(raw[0], &vtable) || !InImage(vtable, imageBase, imageSize)) return false;
			if (!TextGuarded(a_text, &a_out)) {
				static bool s_said = false;
				if (!s_said) { s_said = true; logger::warn("alchemy: an ingredient text faulted when read; skipped"); }
				return false;
			}
			return true;
		}

		void AddIngredientForm(RE::TESForm* a_form)
		{
			auto* ingredient = a_form ? a_form->As<RE::IngredientItem>() : nullptr;
			if (!ingredient) return;
			const char* key = RE::TESFullName::GetFullName(a_form);   // the name's string-table key, "LOC_FN_..."
			if (key) alchemyrules::AddIngredient(g_ingredients, key, ingredient->weight, ingredient->data.costOverride);
		}

		bool WalkForms()
		{
			auto* forms = RE::TESForm::GetAllForms();
			if (!forms) return false;
			for (auto it = forms->begin(); it != forms->end(); ++it) AddIngredientForm(it->second);
			return true;
		}

		bool WalkFormsGuarded()   // no unwinding objects here (C2712): the walk itself is in WalkForms
		{
			__try {
				return WalkForms();
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		// IMP-MR-005: the editor-ID map holds no ingredients at run time (2026-11-04); the full-name key does (as for
		// Inventory rows). Built once: ingredient forms do not change after the game's data is loaded.
		void BuildIngredientIndex()
		{
			if (g_ingredientsBuilt) return;
			g_ingredientsBuilt = true;
			g_ingredients.clear();
			const bool ok = WalkFormsGuarded();
			if (!ok || g_ingredients.empty()) logger::warn("alchemy: no ingredient forms indexed (walk {}); Weight/Value sort by name", ok ? "empty" : "faulted");
		}

		std::pair<float, int> FormData(const std::string& a_name, const UE::FText& a_text)
		{
			if (auto it = g_formData.find(a_name); it != g_formData.end()) return it->second;
			BuildIngredientIndex();
			const auto data = alchemyrules::IngredientStats(g_ingredients, reflect::TextKey(a_text));   // "LOC_FN_..."
			g_formData.emplace(a_name, data);
			g_formMatch.emplace(MatchKey(a_name), data);
			return data;
		}

		bool ReadIngredient(const std::uint8_t* a_props, Ingredient& a_out, bool a_formData, bool a_effects = true)
		{
			const auto& l = Props();
			if (!l.ok || !a_props) return false;
			const auto* name = reinterpret_cast<const UE::FText*>(a_props + l.name);
			if (!SafeText(name, a_out.name)) return false;
			a_out.count = *reinterpret_cast<const std::int32_t*>(a_props + l.quantity);
			const auto* effects = reinterpret_cast<const RawArray*>(a_props + l.effects);
			a_out.knownEffects.clear();
			for (std::int32_t i = 0; a_effects && effects->data && i < effects->num && i < 8; ++i) {
				const auto* e = effects->data + static_cast<std::ptrdiff_t>(i) * l.effectSize;
				if (l.effectUnknown >= 0 && *reinterpret_cast<const bool*>(e + l.effectUnknown) != l.unknownInverted) continue;   // unknown
				std::string effect;
				if (SafeText(reinterpret_cast<const UE::FText*>(e + l.effectName), effect)) a_out.knownEffects.push_back(std::move(effect));
			}
			if (a_formData) {
				const auto [weight, value] = FormData(a_out.name, *name);
				a_out.weight = weight;
				a_out.value = value;
			}
			return true;
		}

		// ---- effect names of filter list items
		bool LooksLikeText(const std::uint8_t* a_p)
		{
			const auto a = *reinterpret_cast<const std::uint64_t*>(a_p);
			const auto b = *reinterpret_cast<const std::uint64_t*>(a_p + 8);
			return a > 0x10000 && b == a - 0x10;   // TSharedRef object + its controller allocated with it
		}

		bool EffectItemName(UE::UObject* a_item, std::string& a_out)
		{
			static UE::UClass* s_class = nullptr;
			if (!a_item || !a_item->GetClass()) return false;
			if (!s_class) {
				if (pe::Utf8(a_item->GetClass()->GetFName().ToString()) != "VModernAlchemyEffect") return false;
				s_class = a_item->GetClass();
			}
			if (a_item->GetClass() != s_class) return false;
			if (g_nameOffset < 0) return false;
			return SafeText(reinterpret_cast<const UE::FText*>(reinterpret_cast<const std::uint8_t*>(a_item) + g_nameOffset), a_out) && !a_out.empty();
		}

		int CountOf(const std::string& a_display)
		{
			const auto it = g_counts.find(MatchKey(a_display));
			return it == g_counts.end() ? 0 : it->second;
		}

		// ---- IMP-MR-004: counts
		void ComputeCounts(UE::UObject* a_menu)
		{
			g_dirtyCounts = false;
			auto* vm = Field(a_menu, "VAlchemyMenuViewModel");
			const auto& l = Props();
			const auto  off = vm ? reflect::Offset(vm->GetClass(), "ItemsList") : -1;
			if (!vm || !l.ok || off < 0) return;
			const auto* arr = reflect::At<RawArray>(vm, off);
			const bool needCounts = NeedCounts(g_countsOpened, g_countsNum, arr->num);
			g_countsOpened = false;
			g_countsNum = arr->num;
			logger::flush();
			std::vector<Ingredient> all;
			for (std::int32_t i = 0; arr->data && i < arr->num && i < 2000; ++i) {
				Ingredient item;
				if (ReadIngredient(arr->data + static_cast<std::ptrdiff_t>(i) * l.size, item, false)) all.push_back(std::move(item));
			}
			// Native rebuilds may reveal a newly learned effect without changing the ingredient count.
			decltype(g_markerEffects) markerEffects;
			for (const auto& item : all) markerEffects[MatchKey(item.name)] = item.knownEffects;
			if (markerEffects != g_markerEffects) g_effectSearchDirty = true;
			g_markerEffects = std::move(markerEffects);
			if (!needCounts) return;
			g_counts = EffectCounts(all);
			static bool s_said = false;
			if (!s_said && all.size() >= 5 && g_counts.empty()) {
				s_said = true;
				logger::warn("alchemy: no known effects read from {} ingredients; filter counts show 0", all.size());
			}
			g_countsReady = true;
		}

		// RMI-024: the count gets its own label in the row (next to the effect name), created once per row with the row text's
		// font and colour; the game's own effect text is never rewritten (its selected look on a recycled row depends on it)
		UE::UObject* CountLabel(UE::UObject* a_row, UE::UObject* a_text)
		{
			if (auto* existing = ui::FindInTree(a_row, "MrAlchemyCount")) return existing;
			auto* box = ui::FindInTree(a_row, "HorizontalBox");
			auto* cls = ui::Class(L"/Script/UMG.TextBlock");
			if (!box || !cls || !box->GetOuter()) return nullptr;
			auto* label = UE::NewObject<UE::UObject>(box->GetOuter(), cls, UE::FName(L"MrAlchemyCount"));
			if (!label) return nullptr;
			ui::Call add(box, L"AddChildToHorizontalBox");
			if (!add) return nullptr;
			add.Set("Content", label);
			add.Run();
			if (auto* slot = add.Get<UE::UObject*>("ReturnValue")) {
				const std::uint8_t centre = 2;   // EVerticalAlignment::VAlign_Center
				ui::CallFirst(slot, L"SetVerticalAlignment", &centre, sizeof(centre));
			}
			for (const char* prop : { "Font", "ColorAndOpacity" }) {   // the row text's own look
				const auto off = reflect::Offset(a_text->GetClass(), prop);
				const auto size = ui::SizeOf(a_text->GetClass(), prop);
				if (off >= 0 && size > 0 && size <= 512)
					ui::CallFirst(label, prop == std::string_view("Font") ? L"SetFont" : L"SetColorAndOpacity", reinterpret_cast<const std::uint8_t*>(a_text) + off, static_cast<std::size_t>(size));
			}
			return label;
		}

		void DecorateRow(UE::UObject* a_row)
		{
			if (!g_countsReady || !a_row) return;
			auto* label = Field(a_row, "EffectText");
			auto* text = label ? ui::FindInTree(label, "text_block") : nullptr;
			if (!text) return;
			const auto shown = uix::GetText(text);
			if (MatchKey(shown).empty()) return;
			const int n = CountOf(shown);
			auto*     count = CountLabel(a_row, text);
			if (count) {
				const auto want = " (" + std::to_string(n) + ")";
				if (uix::GetText(count) != want) uix::SetText(count, want);
				ui::Float(count, L"SetRenderOpacity", n > 0 ? 1.0f : kZeroOpacity);
			}
			ui::Float(label, L"SetRenderOpacity", n > 0 ? 1.0f : kZeroOpacity);
		}

		// the filter item's effect name: the FText at +0x30 (VModernAlchemyEffect has no reflected fields; proven by matching
		// the rows 2026-11-04). Only that one field is read - never a scan (RMI-022: a scan called ToString on garbage).
		constexpr std::int32_t kEffectNameCandidate = 0x30;

		std::vector<std::pair<int, std::string>> ItemTexts(UE::UObject* a_item)
		{
			std::vector<std::pair<int, std::string>> texts;
			if (!a_item || !reflect::IsLive(a_item) || !a_item->GetClass()) return texts;
			if (a_item->GetClass()->propertiesSize < kEffectNameCandidate + static_cast<std::int32_t>(sizeof(UE::FText))) return texts;
			std::string s;
			if (SafeText(reinterpret_cast<const UE::FText*>(reinterpret_cast<const std::uint8_t*>(a_item) + kEffectNameCandidate), s))
				texts.emplace_back(kEffectNameCandidate, std::move(s));
			return texts;
		}

		void OnEntryEvent(UE::UObject* a_row, UE::UFunction* a_fn, void* a_params)
		{
			if (!g_open || !Enabled() || !a_fn) return;
			static UE::UFunction* s_set = nullptr;
			if (!s_set) {
				if (pe::FunctionName(a_fn) != "OnListItemObjectSet") return;
				s_set = a_fn;
			}
			if (a_fn != s_set) return;
			DecorateRow(a_row);
			if (g_itemNames == 0 && a_params) {   // learn the item's name field once from what the row shows
				auto* item = *static_cast<UE::UObject**>(a_params);
				auto* label = Field(a_row, "EffectText");
				auto* text = label ? ui::FindInTree(label, "text_block") : nullptr;
				if (text && item && reflect::IsLive(item) && item->GetClass()) {
					const auto texts = ItemTexts(item);
					const auto size = item->GetClass()->propertiesSize;
					const auto shown = uix::GetText(text);
					g_nameOffset = LearnTextOffset(texts, shown);
					g_itemNames = g_nameOffset >= 0 ? 1 : -1;
				if (g_itemNames == 1) g_effectSearchDirty = true;
					if (g_itemNames < 0) logger::warn("alchemy: no effect name found in the filter item (size {:X}, {} texts, row \"{}\"); one-click removal is off", size, texts.size(), shown);
				}
			}
		}

		// RMI-024 (game bug, vanilla too): a row reused on scroll comes back unselected even when its effect is an active
		// filter. Set the row's own flag again and let its highlight event redraw it (the event draws the selected look
		// from bIsSelected). Done from the tick, never inside the game's own row call.
		void RestoreSelected(UE::UObject* a_row, const std::vector<std::string>& a_active)
		{
			if (a_active.empty() || !a_row || !a_row->GetClass()) return;
			const auto off = reflect::Offset(a_row->GetClass(), "bIsSelected");
			auto*      label = Field(a_row, "EffectText");
			auto*      text = label ? ui::FindInTree(label, "text_block") : nullptr;
			auto*      check = Field(a_row, "Checkmark");
			auto*      back = Field(a_row, "Selected_State");
			if (off < 0 || !text || !check || !back || !check->GetClass()) return;
			// the look a click gives (probe 6): bIsSelected, the diamond (Checkmark) and the dark row (Selected_State) Visible
			const auto vis = reflect::Offset(check->GetClass(), "Visibility");
			auto*      flag = reinterpret_cast<std::uint8_t*>(a_row) + off;
			const bool shown = *flag != 0 && vis >= 0 && *(reinterpret_cast<const std::uint8_t*>(check) + vis) == 0;
			if (!ShouldRestoreSelected(uix::GetText(text), shown, a_active)) return;
			*flag = 1;
			const std::uint8_t visible = 0;   // ESlateVisibility::Visible
			ui::CallFirst(check, L"SetVisibility", &visible, sizeof(visible));
			ui::CallFirst(back, L"SetVisibility", &visible, sizeof(visible));
		}

		bool PadInput();

		void ClearStaleFilterHover(const std::vector<UE::UObject*>& a_rows)
		{
			if (!PadInput()) return;
			UE::UObject* focused = nullptr;
			for (auto* row : a_rows) {
				ui::Call own(row, L"HasAnyUserFocus");
				ui::Call child(row, L"HasFocusedDescendants");
				if ((own && own.Run() && own.Get<bool>("ReturnValue")) ||
				    (child && child.Run() && child.Get<bool>("ReturnValue"))) {
					if (focused) return;
					focused = row;
				}
			}
			if (!focused) return;
			for (auto* row : a_rows) {
				if (row == focused) continue;
				auto* hover = Field(row, "Hovered_State");
				if (!hover || !hover->GetClass()) continue;
				const auto vis = reflect::Offset(hover->GetClass(), "Visibility");
				if (vis >= 0 && *reflect::At<std::uint8_t>(hover, vis) == 0) {
					const std::uint8_t collapsed = 1;
					ui::CallFirst(hover, L"SetVisibility", &collapsed, sizeof(collapsed));
				}
			}
		}

		void DecorateRows(UE::UObject* a_filters, const std::vector<std::string>& a_active)
		{
			auto* list = InnerList(Field(a_filters, "ListView_Filters"));
			if (!list) return;
			const auto rows = DisplayedRows(list);
			ClearStaleFilterHover(rows);
			std::vector<std::string> rowTexts;
			for (auto* row : rows) {
				if (!g_entryClass && row->GetClass()) {
					g_entryClass = row->GetClass();
					pe::WatchPost(g_entryClass, &OnEntryEvent);
				}
				if (g_itemNames == 0) {
					auto* label = Field(row, "EffectText");
					if (auto* text = label ? ui::FindInTree(label, "text_block") : nullptr) rowTexts.push_back(uix::GetText(text));
				}
				DecorateRow(row);
				RestoreSelected(row, a_active);
			}
			if (g_itemNames == 0 && rowTexts.size() >= 3) {   // learn the item's name field from the rows on screen
				std::vector<std::vector<std::pair<int, std::string>>> perItem;
				for (auto* item : ListItems(list)) {
					if (perItem.size() >= 24) break;
					perItem.push_back(ItemTexts(item));
				}
				g_nameOffset = LearnTextOffsetFromRows(perItem, rowTexts);
				g_itemNames = g_nameOffset >= 0 ? 1 : -1;
				if (g_itemNames == 1) g_effectSearchDirty = true;
				if (g_itemNames < 0) logger::warn("alchemy: no effect name found in {} filter items against {} rows; one-click removal is off", perItem.size(), rowTexts.size());
			}
		}

		// ---- IMP-MR-005: sort
		std::string ReadPotionName(UE::UObject* a_name)
		{
			ui::Call get(a_name, L"GetTextValue");
			auto* result = get ? get.At("ReturnValue") : nullptr;
			if (!result || get.Size("ReturnValue") != sizeof(UE::FText) || !get.Run()) return {};
			auto* text = static_cast<UE::FText*>(result);
			const auto value = reflect::Text(*text);
			text->~FText();
			return value;
		}

		bool NameTextCall(UE::UObject* a_name, const wchar_t* a_function, const char* a_parameter, const std::string& a_value, bool a_commit = false)
		{
			ui::Call call(a_name, a_function);
			auto* parameter = call ? call.At(a_parameter) : nullptr;
			if (!parameter || call.Size(a_parameter) != sizeof(UE::FText)) return false;
			const auto wide = textutil::Wide(a_value);
			auto* text = new (parameter) UE::FText(UE::FText::AsCultureInvariant(UE::FString(wide.c_str())));
			if (a_commit) call.Set("CommittedMethod", std::uint8_t{ 1 }); // ETextCommit::OnEnter
			const bool ran = call.Run();
			text->~FText();
			return ran;
		}

		void PotionNameTick(UE::UObject* a_name)
		{
			ui::Call enabled(a_name, L"GetIsEnabled");
			g_canName = a_name && ui::ShownOnScreen(a_name) && enabled && enabled.Run() && enabled.Get<bool>("ReturnValue");
			if (g_nameKeyboardRequest.exchange(false) && g_canName.load() && !keyboard::Taking()) {
				g_nameApplied = ReadPotionName(a_name);
				search::SetText(menus::Menu::kAlchemyName, g_nameApplied);
				ui::Call start(a_name, L"StartEditText"); if (start) start.Run();
				g_nameMode = true;
				if (!keyboard::Open(menus::Menu::kAlchemyName, kPadName)) {
					g_nameMode = false;
					ui::Call stop(a_name, L"StopEditText"); if (stop) stop.Run();
				}
			}
			if (!g_nameMode.load()) return;
			const auto value = search::GetText(menus::Menu::kAlchemyName);
			const bool commit = !keyboard::Taking();
			if (value != g_nameApplied || commit) {
				const bool set = NameTextCall(a_name, L"SetUserInputText", "InText", value);
				const bool change = NameTextCall(a_name, L"UpdateOnTextChanged", "NewUserInputText", value);
				const bool done = !commit || NameTextCall(a_name, L"Update on Text Committed", "NewText", value, true);
				if (set && change && done) g_nameApplied = value;
				else { static bool said = false; if (!said) { said = true; logger::warn("alchemy: potion-name text bridge unavailable"); } }
			}
			if (commit) {
				ui::Call stop(a_name, L"StopEditText"); if (stop) stop.Run();
				g_nameMode = false;
				logger::debug("alchemy: controller potion name committed");
			}
		}

		bool RootSearchSource(UE::UObject* a_menu, const std::vector<UE::UObject*>& a_items, reflect::Handle& a_cache, const wchar_t* a_name)
		{
			auto* cache = reflect::Get(a_cache);
			if (!cache || !ui::IsWithin(cache, a_menu)) {
				cache = ui::FindInTree(a_menu, textutil::Utf8(a_name));
				if (!cache || !ui::IsWithin(cache, a_menu)) {
					auto* canvas = ui::TreeRootCanvas(a_menu);
					UE::UObject* slot = nullptr;
					cache = canvas ? ui::AddToCanvas(canvas, L"/Script/UMG.ListView", a_name, &slot) : nullptr;
				}
				if (!cache) return false;
				ui::Visible(cache, false);
				a_cache = reflect::Hold(cache);
			}
			// The hidden ListView roots the original UObjects while visible matches are removed.
			ui::Call clear(cache, L"ClearListItems");
			if (!clear || !clear.Run()) return false;
			for (auto* item : a_items) ui::CallFirst(cache, L"AddItem", &item, sizeof(item));
			return true;
		}

		bool RootSearchItems(UE::UObject* a_menu, const std::vector<UE::UObject*>& a_items)
		{
			return RootSearchSource(a_menu, a_items, g_searchCache, L"MrAlchemySearchSource");
		}

		bool MatchesQuery(const Ingredient& a_item, const std::string& a_query)
		{
			if (a_query.empty() || textutil::Contains(a_item.name, a_query, searchcfg::Get().ignoreAccents)) return true;
			for (const auto& effect : a_item.knownEffects)
				if (textutil::Contains(effect, a_query, searchcfg::Get().ignoreAccents)) return true;
			return false;
		}

		void ApplySort(UE::UObject* a_menu)
		{
			g_dirtySort = false;
			auto* list = InnerList(Field(a_menu, "IngredientsListView"));
			if (!list) return;
			const auto live = ListItems(list);
			if (auto* cache = reflect::Get(g_searchCache); !cache || !ui::IsWithin(cache, a_menu)) g_searchList.Reset();
			if (g_searchList.Observe(live) && !RootSearchItems(a_menu, live)) { g_searchList.Reset(); return; }
			const auto& items = g_searchList.source;
			if (items.empty()) { g_searchList.Commit(live); return; }
			const auto propsOff = reflect::Offset(items.front()->GetClass(), "Properties");
			if (propsOff < 0) return;
			std::vector<Ingredient> data(items.size());
			const auto query = searchcfg::Get().alchemy && searchcfg::Get().ownFilter ? search::GetText(menus::Menu::kAlchemy) : std::string();
			const bool effects = !query.empty(); // search reads known effects; sorting alone does not
			logger::flush();
			for (std::size_t i = 0; i < items.size(); ++i) {
				if (!ReadIngredient(reinterpret_cast<const std::uint8_t*>(items[i]) + propsOff, data[i], true, effects)) return;   // all or nothing
			}
			static bool s_warned = false;
			if (!s_warned && data.size() >= 5 && std::none_of(data.begin(), data.end(), [](const Ingredient& i) { return i.weight >= 0; })) {
				s_warned = true;
				logger::warn("alchemy: no ingredient weight/value found (name key lookup); those sorts fall back to the name");
			}
			auto order = SortOrder(data, g_sortState.key, g_sortState.descending);
			std::erase_if(order, [&](int i) { return !MatchesQuery(data[static_cast<std::size_t>(i)], query); });
						std::vector<UE::UObject*> sorted;
			sorted.reserve(order.size());
			for (int i : order) sorted.push_back(items[static_cast<std::size_t>(i)]);
			g_searchList.Commit(sorted);
			if (sorted == live) return;
			UE::UObject* selected = nullptr;
			{
				ui::Call get(list, L"BP_GetSelectedItem");
				if (get && get.Run()) selected = get.Get<UE::UObject*>("ReturnValue");
			}
			SetItems(list, sorted);
			if (selected && std::find(sorted.begin(), sorted.end(), selected) != sorted.end()) {
				ui::Call set(list, L"BP_SetSelectedItem");
				if (set) { set.Set("Item", selected); set.Run(); }
			}
		}

		// ---- IMP-MR-006: the selected filters
		bool LooksLikeFString(const std::uint8_t* a_elem)
		{
			__try {
				const auto* data = *reinterpret_cast<const wchar_t* const*>(a_elem);
				const auto  num = *reinterpret_cast<const std::int32_t*>(a_elem + 8);
				const auto  max = *reinterpret_cast<const std::int32_t*>(a_elem + 12);
				return data && num >= 1 && num <= 512 && max >= num && data[num - 1] == 0;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		std::vector<std::string> SelectedNames(UE::UObject* a_menu)
		{
			std::vector<std::string> out;
			auto*    vm = Field(a_menu, "VAlchemyMenuViewModel");
			ui::Call g(vm, L"GetEffectFiltersSelectedNames");
			void*    ret = g ? g.At("ReturnValue") : nullptr;
			if (!ret || g.Size("ReturnValue") != 16 || !g.Run()) return out;
			auto* arr = static_cast<RawArray*>(ret);
			if (!arr->data || arr->num <= 0) return out;
			static int s_kind = 0;   // 1 FString elements, 2 FText elements, -1 neither
			if (s_kind == 0) {
				s_kind = LooksLikeFString(arr->data) ? 1 : LooksLikeText(arr->data) ? 2 : -1;
				if (s_kind < 0) logger::warn("alchemy: selected filter names have an unknown type; the active-filter line is off");
			}
			for (std::int32_t i = 0; s_kind > 0 && i < arr->num && i < 64; ++i) {
				if (s_kind == 1) {
					auto* s = reinterpret_cast<UE::FString*>(arr->data + static_cast<std::ptrdiff_t>(i) * 16);
					out.push_back(pe::Utf8(*s));
					s->~FString();
				} else {
					auto* t = reinterpret_cast<UE::FText*>(arr->data + static_cast<std::ptrdiff_t>(i) * sizeof(UE::FText));
					std::string name;
					if (SafeText(t, name)) out.push_back(std::move(name));
					t->~FText();
				}
			}
			UE::FMemory::Free(arr->data);
			arr->data = nullptr;
			arr->num = arr->max = 0;
			return out;
		}

		UE::UObject* FindFilterItem(UE::UObject* a_list, const std::string& a_key)
		{
			for (auto* item : ListItems(a_list)) {
				std::string name;
				if (EffectItemName(item, name) && MatchKey(name) == a_key) return item;
			}
			return nullptr;
		}

		bool ClickTab(UE::UObject* a_filters, bool a_positive)
		{
			const auto fn = FindFunction(a_filters->GetClass(), { a_positive ? "PositiveEffectsButton" : "NegativeEffectsButton", "Clicked" });
			auto*      button = Field(a_filters, a_positive ? "PositiveEffectsButton" : "NegativeEffectsButton");
			return !fn.empty() && button && ui::CallFirst(a_filters, fn.c_str(), &button, sizeof(button));
		}

		// turn one filter off the way a click on its row does; the item may sit on the other tab
		void RemoveFilter(UE::UObject* a_menu, const std::string& a_display)
		{
			auto* filters = Field(a_menu, "Alchemy_Filters");
			auto* list = InnerList(Field(filters, "ListView_Filters"));
			if (!filters || !list || g_itemNames != 1) return;
			const auto click = FindFunction(filters->GetClass(), { "ListView_Filters", "OnItemClicked" });
			if (click.empty()) return;
			const auto key = MatchKey(a_display);
			auto*      item = FindFilterItem(list, key);
			bool       switched = false;
			const bool positive = Flag(filters, "bIsOnPostiveEffect");
			if (!item && ClickTab(filters, positive)) {
				g_effectSearchDirty = true;
				item = FindFilterItem(list, key);
			}
			if (!item && ClickTab(filters, !positive)) {
				switched = true;
				item = FindFilterItem(list, key);
			}
			if (item) ui::CallFirst(filters, click.c_str(), &item, sizeof(item));
			if (switched) ClickTab(filters, positive);
		}

		using FilterTab = alchemytabs::Tab;
		FilterTab g_filterTab = FilterTab::All;
		bool g_filterOpen = false, g_settingTab = false;
		std::atomic<int> g_tabRequest{ -1 }, g_tabCycle{ 0 };
		std::atomic<bool> g_filtersVisible{ false };
		std::array<reflect::Handle, 3> g_tabLabels, g_tabLeft, g_tabRight, g_tabDashLeft, g_tabDashRight, g_tabUpperLeft, g_tabUpperRight, g_tabLowerLeft, g_tabLowerRight;
		reflect::Handle g_tabOwner;
		std::atomic<int> g_hoverTab{ -1 };
		std::array<Rect, 3> g_tabRects{};

		// Clear all: each filter off as a single entry does (the menu's clear-all input-event stub does nothing when called)
		void RemoveEveryFilter(UE::UObject* a_menu)
		{
			for (const auto& name : SelectedNames(a_menu)) RemoveFilter(a_menu, name);
		}



		void ApplyEffectSearch(UE::UObject* a_filters)
		{
			if (!g_effectSearchReady) return;
			auto* list = InnerList(Field(a_filters, "ListView_Filters"));
			if (!list) return;
			std::vector<UE::UObject*> matches;
			for (auto* item : g_effectSearchItems) {
				std::string name;
				if (g_effectSearchQuery.empty() || !EffectItemName(item, name) ||
					textutil::Contains(name, g_effectSearchQuery, searchcfg::Get().ignoreAccents)) matches.push_back(item);
			}
			if (ListItems(list) != matches) SetItems(list, matches);
		}

		void ShowFilterTab(UE::UObject* a_filters, FilterTab a_tab)
		{
			auto* list = InnerList(Field(a_filters, "ListView_Filters"));
			if (!list) return;
			g_settingTab = true;
			// Read fresh native lists: no long-lived raw object cache, and zero-count
			// filtering cannot permanently remove entries from the combined source.
			ClickTab(a_filters, false);
			auto negative = ListItems(list);
			ClickTab(a_filters, true);
			auto positive = ListItems(list);
			if (a_tab == FilterTab::Negative) ClickTab(a_filters, false);
			const auto source = alchemytabs::Items(a_tab, positive, negative);
			g_effectSearchReady = RootSearchSource(Widget(), source, g_effectSearchCache, L"MrAlchemyFilterSearchSource");
			g_effectSearchItems = g_effectSearchReady ? source : std::vector<UE::UObject*>{};
			SetItems(list, source);
			ApplyEffectSearch(a_filters);
			static bool announced = false;
			if (!announced) { announced = true; logger::warn("alchemy: All / Positive / Negative recreated filter tabs ready (inward ornaments and full effect labels)"); }
			g_settingTab = false;
			g_filterTab = a_tab;
			g_nextLayout = {};
		}

		// ---- our widgets
		UE::UObject* MakeLabel(UE::UObject* a_canvas, const wchar_t* a_name, const float* a_ink, double a_size)
		{
			UE::UObject* slot = nullptr;
			auto*        label = ui::AddToCanvas(a_canvas, L"/Script/UMG.TextBlock", a_name, &slot);
			if (!label || !slot) return nullptr;
			const bool yes = true;
			ui::CallFirst(slot, L"SetAutoSize", &yes, sizeof(yes));
			const int z = 50;
			ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			uix::Ink(label, a_ink);
			uix::ApplyFont(label, a_size);
			ui::Visible(label, false);
			return label;
		}

		UE::UObject* SlotOf(UE::UObject* a_widget) { return Field(a_widget, "Slot"); }

		void Place(UE::UObject* a_label, double a_x, double a_y, double a_alignX)
		{
			auto* slot = SlotOf(a_label);
			if (!slot) return;
			ui::Vec2(slot, L"SetAlignment", a_alignX, 0.5);
			ui::Vec2(slot, L"SetPosition", a_x, a_y);
		}

		std::array<double, 2> DesiredSize(UE::UObject* a_label)
		{
			ui::Call size(a_label, L"GetDesiredSize");
			if (!size || !size.Run()) return { 0, 0 };
			return size.Get<std::array<double, 2>>("ReturnValue");
		}

		double DesiredWidth(UE::UObject* a_label) { return DesiredSize(a_label)[0]; }

		UE::UObject* StripTexture()
		{
			if (auto* t = reflect::Get(g_stripTexture)) return t;
			static bool s_missing = false;
			if (s_missing) return nullptr;
			const auto path = mrconfig::PluginFolder() / L"MenusRevamped" / L"Alchemy" / kStripFile;
			auto*      t = ui::ImportTexture(path);
			if (!t) {
				s_missing = true;
				logger::warn("alchemy: missing or unreadable {}; no parchment strip above the Ingredients panel", path.filename().string());
				return nullptr;
			}
			g_stripTexture = reflect::Hold(t);
			return t;
		}

		// the strip: on top of the menu's own panels (owner 2026-11-04, as in the owner's PSD), under our labels and buttons
		UE::UObject* MakeStrip(UE::UObject* a_canvas)
		{
			auto* texture = StripTexture();
			if (!texture) return nullptr;
			UE::UObject* slot = nullptr;
			auto*        image = ui::AddToCanvas(a_canvas, L"/Script/UMG.Image", L"MrAlchemyStrip", &slot);
			if (!image || !slot) return nullptr;
			const int z = 48;
			ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			ui::Call set(image, L"SetBrushFromTexture");
			set.Set("Texture", texture);
			set.Set("bMatchSize", false);
			set.Run();
			ui::Visible(image, false);
			return image;
		}

		UE::UObject* MakeBack(UE::UObject* a_canvas, std::size_t a_index)
		{
			UE::UObject* slot = nullptr;
			auto*        image = ui::AddToCanvas(a_canvas, L"/Script/UMG.Image", (L"MrAlchemyChipBack" + std::to_wstring(a_index)).c_str(), &slot);
			if (!image || !slot) return nullptr;
			const int z = 49;   // under the label
			ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			ui::Visible(image, false);
			return image;
		}

		void StyleBack(std::size_t a_index, bool a_hovered)
		{
			if (a_index >= g_chipBacks.size() || a_index >= g_chipStyle.size()) return;
			auto*     back = reflect::Get(g_chipBacks[a_index]);
			const int want = a_hovered ? 1 : 0;
			if (!back || g_chipStyle[a_index] == want) return;
			// Clear all keeps the red rim; a filter button gets a black one (owner 2026-11-04)
			const float* line = a_index == 0 ? kButtonLine : kButtonLineDark;
			if (ui::RoundedBox(back, false, a_hovered ? kButtonHover : kButtonFill, line, 0.5f, 6.0f)) g_chipStyle[a_index] = want;
		}

		double MarkWidth()
		{
			static double s_width = 0;
			if (s_width <= 0) {
				for (auto& h : g_chipMarks) {
					auto* mark = reflect::Get(h);
					const double w = mark ? DesiredWidth(mark) : 0;
					if (w > 0) { s_width = w; break; }
				}
			}
			return s_width > 0 ? s_width : 8.0;
		}

		Rect PixelRect(UE::UObject* a_widget, double a_scale)
		{
			Rect r;
			if (a_widget && ui::Measure(a_widget, r.x, r.y, r.w, r.h)) {
				r = { r.x * a_scale, r.y * a_scale, r.w * a_scale, r.h * a_scale, r.w > 0 && r.h > 0 };
			}
			return r;
		}

		void HideOwn()
		{
			for (auto& h : g_chipCircles) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
			for (auto& h : g_icons) if (auto* l = reflect::Get(h)) ui::Visible(l, false);
			for (auto& h : g_iconBacks) if (auto* l = reflect::Get(h)) ui::Visible(l, false);
			if (auto* l = reflect::Get(g_arrow)) ui::Visible(l, false);
			for (auto& h : g_seps) if (auto* l = reflect::Get(h)) ui::Visible(l, false);
			if (auto* l = reflect::Get(g_rule)) ui::Visible(l, false);
			for (auto& h : g_chips) {
				if (auto* l = reflect::Get(h)) ui::Visible(l, false);
			}
			for (auto& h : g_chipBacks) {
				if (auto* l = reflect::Get(h)) ui::Visible(l, false);
			}
			if (auto* l = reflect::Get(g_strip)) ui::Visible(l, false);
			if (auto* l = reflect::Get(g_more)) ui::Visible(l, false);
			if (auto* l = reflect::Get(g_chipTitle)) ui::Visible(l, false);
			for (auto& h : g_chipMarks) {
				if (auto* l = reflect::Get(h)) ui::Visible(l, false);
			}
			for (const auto& h : g_tabLabels) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
			for (const auto& h : g_tabLeft) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
			for (const auto& h : g_tabRight) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
			for (const auto& h : g_tabDashLeft) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
			for (const auto& h : g_tabDashRight) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
			for (const auto& group : {g_tabUpperLeft, g_tabUpperRight, g_tabLowerLeft, g_tabLowerRight})
				for (const auto& h : group) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
			g_hoverTab = -1;
			g_hoverChip = -1;
			std::scoped_lock lock(g_rectLock);
			g_iconRects = {};
			g_chipRects.clear();
			g_tabRects = {};
		}

		UE::UObject* MakeTabOrnament(UE::UObject* a_canvas, UE::UObject* a_source, const wchar_t* a_name)
		{
			UE::UObject* slot = nullptr;
			auto* image = ui::AddToCanvas(a_canvas, L"/Script/UMG.Image", a_name, &slot);
			auto* source = a_source ? ui::FindInTree(a_source, "Image") : nullptr;
			if (!image || !slot || !source) return nullptr;
			const auto off = reflect::Offset(source->GetClass(), "Brush");
			const auto size = ui::SizeOf(source->GetClass(), "Brush");
			if (off < 0 || size <= 0 || size > 1024) return nullptr;
			ui::CallFirst(image, L"SetBrush", reinterpret_cast<const std::uint8_t*>(source) + off, size);
			const int z = 50;
			ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			return image;
		}

		void CopyIngredientsFont(UE::UObject* a_label, UE::UObject* a_title)
		{
			auto* text = a_title;
			if (text && reflect::Offset(text->GetClass(), "Font") < 0) text = ui::FindInTree(text, "text_block");
			if (!text) return;
			const auto off = reflect::Offset(text->GetClass(), "Font");
			const auto size = ui::SizeOf(text->GetClass(), "Font");
			if (off < 0 || size <= 0 || size > 512) return;
			auto* fontInfo = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateFontInfo"));
			const auto sizeOff = fontInfo ? reflect::Offset(fontInfo, "Size") : -1;
			const auto sizeBytes = fontInfo ? ui::SizeOf(fontInfo, "Size") : -1;
			if (sizeOff < 0 || sizeOff + sizeBytes > size || (sizeBytes != 4 && sizeBytes != 8)) return;
			const auto* data = reinterpret_cast<const std::uint8_t*>(text) + off;
			std::vector<std::uint8_t> font(data, data + size);
			if (sizeBytes == 4) { const float value = 21; std::memcpy(font.data() + sizeOff, &value, 4); }
			else { const double value = 21; std::memcpy(font.data() + sizeOff, &value, 8); }
			ui::CallFirst(a_label, L"SetFont", font.data(), font.size());
		}

		void CopyTabFont(UE::UObject* a_label, UE::UObject* a_original)
		{
			auto* prefab = Field(a_original, "focus_text");
			auto* text = prefab ? ui::FindInTree(prefab, "text_block") : nullptr;
			if (!text) return;
			const auto off = reflect::Offset(text->GetClass(), "Font");
			const auto size = ui::SizeOf(text->GetClass(), "Font");
			if (off >= 0 && size > 0 && size <= 512)
				ui::CallFirst(a_label, L"SetFont", reinterpret_cast<const std::uint8_t*>(text) + off, size);
		}

		void LayoutFilterTabs(UE::UObject* a_menu)
		{
			auto* filters = Field(a_menu, "Alchemy_Filters");
			const bool shown = filters && Flag(filters, "bIsActive");
			if (!shown) {
				for (const auto& group : {g_tabLabels, g_tabLeft, g_tabRight, g_tabDashLeft, g_tabDashRight, g_tabUpperLeft, g_tabUpperRight, g_tabLowerLeft, g_tabLowerRight})
					for (const auto& h : group) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
				g_hoverTab = -1;
				std::scoped_lock lock(g_rectLock); g_tabRects = {}; return;
			}
			auto* positive = Field(filters, "PositiveEffectsButton");
			auto* negative = Field(filters, "NegativeEffectsButton");
			Box p, n;
			if (!ui::Measure(positive, p.x, p.y, p.w, p.h) || !ui::Measure(negative, n.x, n.y, n.w, n.h) || p.w <= 0 || n.w <= 0) return;
			auto* canvas = ui::TreeRootCanvas(a_menu);
			if (!canvas) return;
			if (reflect::Get(g_tabOwner) != a_menu) {
				g_tabLabels = {}; g_tabLeft = {}; g_tabRight = {}; g_tabDashLeft = {}; g_tabDashRight = {};
				g_tabUpperLeft = {}; g_tabUpperRight = {}; g_tabLowerLeft = {}; g_tabLowerRight = {};
				g_tabOwner = reflect::Hold(a_menu);
				g_hoverTab = -1;
			}
			// Read original brush assets only; no live Blueprint button instances or style calls.
			ui::Call negParent(negative, L"GetParent");
			auto* dashRow = negParent && negParent.Run() ? negParent.Get<UE::UObject*>("ReturnValue") : nullptr;
			auto* dashLeft = dashRow ? ui::FindInTree(dashRow, "PauseMenuDecoLeft_1") : nullptr;
			Box dashSize{0, 0, 24, 20};
			ui::Measure(dashLeft, dashSize.x, dashSize.y, dashSize.w, dashSize.h);
			dashSize.w *= 0.8;
			dashSize.h *= 0.8;
			Box leftHint, rightHint;
			double startX = p.x, endX = n.x + n.w;
			if (ui::Measure(ui::FindInTree(filters, "NavigateLeft_InputHint"), leftHint.x, leftHint.y, leftHint.w, leftHint.h) &&
			    ui::Measure(ui::FindInTree(filters, "NavigateRight_InputHint"), rightHint.x, rightHint.y, rightHint.w, rightHint.h) &&
			    rightHint.x > leftHint.x + leftHint.w) {
				startX = leftHint.x + leftHint.w + 12;
				endX = rightHint.x - 12;
			}
			const double totalWidth = endX - startX;
			const std::array<double, 3> widths{ totalWidth * 0.18, totalWidth * 0.41, totalWidth * 0.41 };
			double cellX = startX;
			const double scale = ui::ViewportScale();
			std::array<Rect, 3> rects{};
			constexpr const char* names[] = { "All", "Positive Effects", "Negative Effects" };
			for (int i = 0; i < 3; ++i) {
				auto* label = reflect::Get(g_tabLabels[i]);
				const auto suffix = std::to_wstring(i);
				if (!label) {
					label = MakeLabel(canvas, (L"MrAlchemyTabText" + suffix).c_str(), kTitleInk, 19);
					if (!label) continue;
					g_tabLabels[i] = reflect::Hold(label);
					uix::SetText(label, names[i]);
					CopyTabFont(label, positive);
				}
				for (int stroke = 0; stroke < 3; ++stroke) for (int side = 0; side < 2; ++side) {
					auto& handles = stroke == 0 ? (side == 0 ? g_tabLeft : g_tabRight) :
					                stroke == 1 ? (side == 0 ? g_tabUpperLeft : g_tabUpperRight) :
					                              (side == 0 ? g_tabLowerLeft : g_tabLowerRight);
					if (!reflect::Get(handles[i])) {
						const auto name = L"MrAlchemyArrow" + suffix + L"_" + std::to_wstring(stroke) + L"_" + std::to_wstring(side);
						auto* image = MakeTabOrnament(canvas, dashLeft, name.c_str());
						if (image) handles[i] = reflect::Hold(image);
					}
				}
				if (!reflect::Get(g_tabDashLeft[i])) {
					auto* image = MakeTabOrnament(canvas, dashLeft, (L"MrAlchemyTabDashLeft" + suffix).c_str());
					if (image) g_tabDashLeft[i] = reflect::Hold(image);
				}
				if (!reflect::Get(g_tabDashRight[i])) {
					auto* image = MakeTabOrnament(canvas, dashLeft, (L"MrAlchemyTabDashRight" + suffix).c_str());
					if (image) g_tabDashRight[i] = reflect::Hold(image);
				}
				const bool selected = static_cast<int>(g_filterTab) == i;
				const bool hovered = g_hoverTab.load() == i;
				const double width = widths[i];
				const double center = cellX + width / 2;
				Place(label, center, p.y + p.h / 2, 0.5);
				ui::Float(label, L"SetRenderOpacity", selected || hovered ? 1.0f : 0.5f);
				ui::Visible(label, true);
				const double halfText = DesiredWidth(label) / 2;
				for (int stroke = 0; stroke < 3; ++stroke) for (int side = 0; side < 2; ++side) {
					const auto& handles = stroke == 0 ? (side == 0 ? g_tabLeft : g_tabRight) :
					                      stroke == 1 ? (side == 0 ? g_tabUpperLeft : g_tabUpperRight) :
					                                    (side == 0 ? g_tabLowerLeft : g_tabLowerRight);
					auto* image = reflect::Get(handles[i]);
					if (!image) continue;
					const double tip = side == 0 ? center - halfText - 8 : center + halfText + 8;
					auto* slot = SlotOf(image);
					ui::Vec2(slot, L"SetPosition", tip - dashSize.w, p.y + (p.h - dashSize.h) / 2);
					ui::Vec2(slot, L"SetSize", dashSize.w, dashSize.h);
					ui::Vec2(image, L"SetRenderTransformPivot", 1.0, 0.5);
					ui::Vec2(image, L"SetRenderScale", side == 0 ? 1.0 : -1.0, 1.0);
					const float angle = (stroke == 0 ? 0.0f : stroke == 1 ? 24.0f : -24.0f) * (side == 0 ? 1.0f : -1.0f);
					ui::Float(image, L"SetRenderTransformAngle", angle);
					ui::Visible(image, selected);
				}
				for (int side = 0; side < 2; ++side) {
					auto* image = reflect::Get(side == 0 ? g_tabDashLeft[i] : g_tabDashRight[i]);
					if (!image) continue;
					auto* slot = SlotOf(image);
					const double x = side == 0 ? center - halfText - dashSize.w - 8 : center + halfText + 8;
					ui::Vec2(slot, L"SetPosition", x, p.y + (p.h - dashSize.h) / 2);
					ui::Vec2(slot, L"SetSize", dashSize.w, dashSize.h);
					ui::Vec2(image, L"SetRenderTransformPivot", 0.5, 0.5);
					ui::Vec2(image, L"SetRenderScale", side == 0 ? 1.0 : -1.0, 1.0);
					ui::Float(image, L"SetRenderOpacity", hovered ? 1.0f : 0.5f);
					ui::Visible(image, !selected);
				}
				rects[i] = { cellX * scale, p.y * scale, width * scale, p.h * scale, true };
				cellX += width;
			}
			// Original visuals stay transparent but keep layout for native list/input behavior.
			for (auto* original : {positive, negative}) {
				ui::Float(original, L"SetRenderOpacity", 0.0f);
				ui::Call getParent(original, L"GetParent");
				auto* row = getParent && getParent.Run() ? getParent.Get<UE::UObject*>("ReturnValue") : nullptr;
				for (const char* name : {"PauseMenuDecoLeft", "PauseMenuDecoRight"}) {
					const auto actual = std::string(name) + (original == negative ? "_1" : "");
					if (auto* deco = row ? ui::FindInTree(row, actual) : nullptr) ui::Float(deco, L"SetRenderOpacity", 0.0f);
				}
			}
			std::scoped_lock lock(g_rectLock);
			g_tabRects = rects;
		}

		// ---- IMP-MR-005: native-style sort header and the Weight / Value columns (owner 2026-11-04)
		UE::UObject* NativeTexture(int a_index)
		{
			if (auto* t = reflect::Get(g_tex[static_cast<std::size_t>(a_index)])) return t;
			static std::array<bool, kTexCount> s_missing{};
			if (s_missing[static_cast<std::size_t>(a_index)]) return nullptr;
			auto* t = ui::Load(kTexPaths[a_index]);
			if (!t) {
				s_missing[static_cast<std::size_t>(a_index)] = true;
				logger::warn("alchemy: game texture {} not found; that sort icon is blank", pe::Utf8(UE::FString(kTexPaths[a_index])));
				return nullptr;
			}
			g_tex[static_cast<std::size_t>(a_index)] = reflect::Hold(t);
			return t;
		}

		void SetTexture(UE::UObject* a_image, UE::UObject* a_texture)
		{
			if (!a_image || !a_texture) return;
			ui::Call set(a_image, L"SetBrushFromTexture");
			set.Set("Texture", a_texture);
			set.Set("bMatchSize", false);
			set.Run();
		}

		UE::UObject* MakeImage(UE::UObject* a_canvas, const wchar_t* a_name, UE::UObject* a_texture, int a_z, double a_size)
		{
			UE::UObject* slot = nullptr;
			auto*        image = ui::AddToCanvas(a_canvas, L"/Script/UMG.Image", a_name, &slot);
			if (!image || !slot) return nullptr;
			ui::CallFirst(slot, L"SetZOrder", &a_z, sizeof(a_z));
			ui::Vec2(slot, L"SetSize", a_size, a_size);
			ui::Vec2(slot, L"SetAlignment", 0.5, 0.5);
			SetTexture(image, a_texture);
			ui::Visible(image, false);
			return image;
		}

		void ResetHeader()
		{
			g_icons = {};
			g_iconBacks = {};
			g_arrow = {};
			g_arrowDir = -1;
			g_seps = {};
			g_rule = {};
		}

		// the list moves down by kHeaderGap (its slot's top padding, added once per list widget) to make room for the
		// icon line between the Ingredients title and the rows (owner 2026-11-04)
		void PadList(UE::UObject* a_wrapper, double a_gap, reflect::Handle& a_padded = g_paddedList)
		{
			if (!a_wrapper || reflect::Get(a_padded) == a_wrapper) return;
			auto* slot = SlotOf(a_wrapper);
			const auto off = slot ? reflect::Offset(slot->GetClass(), "Padding") : -1;
			if (off < 0 || ui::SizeOf(slot->GetClass(), "Padding") != 16) {
				static bool s_said = false;
				if (!s_said) { s_said = true; logger::warn("alchemy: the ingredient list's slot has no padding; no room made for the sort icons"); }
				a_padded = reflect::Hold(a_wrapper);
				return;
			}
			float margin[4];   // FMargin: left, top, right, bottom
			std::memcpy(margin, reinterpret_cast<const std::uint8_t*>(slot) + off, sizeof(margin));
			margin[1] += static_cast<float>(a_gap);
			ui::CallFirst(slot, L"SetPadding", margin, sizeof(margin));
			a_padded = reflect::Hold(a_wrapper);
		}

		// the icons on their own line in that gap: lightning (Count) and A-Z at the list's left, weight and coin centred
		// over their columns (owner 2026-11-04 screenshot)
		std::array<Rect, kIcons> LayoutHeader(UE::UObject* a_canvas, UE::UObject* a_menu, double a_scale)
		{
			std::array<Rect, kIcons> rects{};
			auto*        wrapper = Field(a_menu, "IngredientsListView");
			const double px = a_scale > 0 ? 1.0 / a_scale : 1.0;   // one screen pixel in viewport units
			const double gap = kHeaderGap - kGapTrimPx * px;
			PadList(wrapper, gap + (searchcfg::Get().alchemy ? kSearchSpace : 0));
			Box        lb, rb;
			const bool haveList = wrapper && ui::Measure(wrapper, lb.x, lb.y, lb.w, lb.h) && lb.w > 0;
			auto*      list = InnerList(wrapper);
			const auto rows = list ? DisplayedRows(list) : std::vector<UE::UObject*>{};
			const bool haveRow = !rows.empty() && ui::Measure(rows.front(), rb.x, rb.y, rb.w, rb.h) && rb.w > 0;
			const bool room = haveList;
			for (int i = 0; i < kIcons; ++i) {
				auto& ih = g_icons[static_cast<std::size_t>(i)];
				auto& bh = g_iconBacks[static_cast<std::size_t>(i)];
				if (!reflect::Get(bh)) {
					if (auto* b = MakeImage(a_canvas, (L"MrAlchemySortBack" + std::to_wstring(i)).c_str(), NativeTexture(kTexBack), 49, kIconBack)) bh = reflect::Hold(b);
				}
				if (!reflect::Get(ih)) {
					if (auto* m = MakeImage(a_canvas, (L"MrAlchemySortIcon" + std::to_wstring(i)).c_str(), NativeTexture(i), 50, kIconSize)) ih = reflect::Hold(m);
				}
			}
			if (!reflect::Get(g_arrow)) {
				if (auto* a = MakeImage(a_canvas, L"MrAlchemySortArrow", nullptr, 51, kArrowSize)) { g_arrow = reflect::Hold(a); g_arrowDir = -1; }
			}
			for (std::size_t i = 0; i < g_seps.size(); ++i) {
				if (reflect::Get(g_seps[i])) continue;
				if (auto* sep = MakeImage(a_canvas, (L"MrAlchemySortSep" + std::to_wstring(i)).c_str(), NativeTexture(kTexLine), 50, kIconSize)) {
					if (auto* slot = SlotOf(sep)) ui::Vec2(slot, L"SetSize", kSepWidth, kIconSize);
					g_seps[i] = reflect::Hold(sep);
				}
			}
			if (!reflect::Get(g_rule)) {   // the column line turned 90 degrees, so it looks like the separators
				if (auto* rule = MakeImage(a_canvas, L"MrAlchemySortRule", NativeTexture(kTexLine), 50, kIconSize)) {
					ui::Float(rule, L"SetRenderTransformAngle", 90.0f);
					g_rule = reflect::Hold(rule);
				}
			}
			const double rowRight = haveRow ? rb.x + rb.w : lb.x + lb.w;
			const auto   xs = HeaderX(lb.x + kLeftShiftPx * px, rowRight, kFirstIcon, kIconStep, kWeightCx, kValueCx);
			const double cy = lb.y - gap * 0.5 - kIconLiftPx * px;
			const int    active = static_cast<int>(g_sortState.key);
			const int    focus = g_padFocus.load(), hover = g_hoverIcon.load();
			for (int i = 0; i < kIcons; ++i) {
				auto*        icon = reflect::Get(g_icons[static_cast<std::size_t>(i)]);
				auto*        back = reflect::Get(g_iconBacks[static_cast<std::size_t>(i)]);
				const double cx = xs[static_cast<std::size_t>(i)];
				if (icon) {
					if (auto* slot = SlotOf(icon)) ui::Vec2(slot, L"SetPosition", cx, cy);
					ui::Visible(icon, room);
					if (room) rects[static_cast<std::size_t>(i)] = PixelRect(icon, a_scale);
				}
				if (back) {
					if (auto* slot = SlotOf(back)) ui::Vec2(slot, L"SetPosition", cx, cy);
					const bool   lit = i == active || i == focus || i == hover;
					const float* tint = i == active ? kActiveTint : kFocusTint;
					if (lit) ui::Colour(back, tint[0], tint[1], tint[2], tint[3]);
					ui::Visible(back, room && lit);
				}
			}
			// separators halfway between lightning (Count) and A-Z (Name), and between Weight and Value
			const double sepX[2] = { (xs[0] + xs[1]) * 0.5, (xs[2] + xs[3]) * 0.5 };
			for (std::size_t i = 0; i < g_seps.size(); ++i) {
				auto* sep = reflect::Get(g_seps[i]);
				if (!sep) continue;
				if (auto* slot = SlotOf(sep)) ui::Vec2(slot, L"SetPosition", sepX[i], cy);
				ui::Visible(sep, room);
			}
			if (auto* rule = reflect::Get(g_rule)) {   // between the icons' bottom and the first row, list left to row right
				const double length = (std::max)(0.0, rowRight - lb.x);
				if (auto* slot = SlotOf(rule)) {
					ui::Vec2(slot, L"SetSize", kSepWidth, length);   // before the turn: width x length
					ui::Vec2(slot, L"SetPosition", (lb.x + rowRight) * 0.5, (cy + kIconSize * 0.5 + lb.y) * 0.5);
				}
				ui::Visible(rule, room && length > 0);
			}
			if (auto* arrow = reflect::Get(g_arrow)) {
				const int dir = g_sortState.descending ? 1 : 0;   // up = A-Z / lowest first, down = Z-A / highest first
				if (dir != g_arrowDir) {
					SetTexture(arrow, NativeTexture(dir ? kTexDown : kTexUp));
					g_arrowDir = dir;
				}
				const double ax = xs[static_cast<std::size_t>(active)];
				if (auto* slot = SlotOf(arrow)) ui::Vec2(slot, L"SetPosition", ax + (kIconBack - kArrowSize) * 0.5, cy + (kIconBack - kArrowSize) * 0.5);
				ui::Visible(arrow, room);
			}
			return rects;
		}

		UE::UObject* RowNameText(UE::UObject* a_row)
		{
			auto* name = Field(a_row, "EntryName");
			if (!name) return nullptr;
			if (reflect::Offset(name->GetClass(), "Font") >= 0) return name;
			return ui::FindInTree(name, "text_block");
		}

		// a column label in the row's own canvas, anchored to its right edge, with the name text's font and colour
		UE::UObject* ColumnLabel(UE::UObject* a_row, UE::UObject* a_text, const wchar_t* a_name, double a_fromRight)
		{
			auto* canvas = ui::FindInTree(a_row, "CanvasPanel");
			if (!canvas) return nullptr;
			UE::UObject* slot = nullptr;
			auto*        label = ui::AddToCanvas(canvas, L"/Script/UMG.TextBlock", a_name, &slot);
			if (!label || !slot) return nullptr;
			const bool yes = true;
			ui::CallFirst(slot, L"SetAutoSize", &yes, sizeof(yes));
			ui::Anchors(slot, 1.0, 0.5, 1.0, 0.5);
			ui::Vec2(slot, L"SetAlignment", 0.5, 0.5);
			ui::Vec2(slot, L"SetPosition", -a_fromRight, g_columnDy);
			if (g_dyKnown) g_dyDone.insert(label);
			const int z = 5;
			ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			for (const char* prop : { "Font", "ColorAndOpacity" }) {
				const auto off = reflect::Offset(a_text->GetClass(), prop);
				const auto size = ui::SizeOf(a_text->GetClass(), prop);
				if (off >= 0 && size > 0 && size <= 512)
					ui::CallFirst(label, prop == std::string_view("Font") ? L"SetFont" : L"SetColorAndOpacity", reinterpret_cast<const std::uint8_t*>(a_text) + off, static_cast<std::size_t>(size));
			}
			return label;
		}

		// IMP-MR-011: named widgets belong to the row/menu widget tree, not the canvas itself.
		UE::UObject* EffectCircle(UE::UObject* a_canvas, UE::UObject* a_owner, const wchar_t* a_name)
		{
			const std::wstring wide(a_name);
			std::string name;
			for (const wchar_t c : wide) name.push_back(static_cast<char>(c)); // stable names are ASCII
			auto* image = ui::FindInTree(a_owner, name.c_str());
			if (image && !ui::IsWithin(image, a_owner)) return nullptr;
			if (!image) {
				UE::UObject* slot = nullptr;
				image = ui::AddToCanvas(a_canvas, L"/Script/UMG.Image", a_name, &slot);
				if (!image || !slot) return nullptr;
				const int z = 51;
				ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			}
			return image;
		}

		void DecorateIngredientMarkers(UE::UObject* a_row, const std::string& a_name)
		{
			auto* canvas = ui::FindInTree(a_row, "CanvasPanel");
			if (!canvas) return;
			const auto it = g_markerEffects.find(MatchKey(a_name));
			const auto markers = it == g_markerEffects.end() ? std::vector<int>{} : IngredientMarkers(it->second, g_markerNames);
			const auto outline = MarkerOutline();
			const float line = static_cast<float>(1.0 / (std::max)(0.01, static_cast<double>(ui::ViewportScale())));
			for (int i = 0; i < kIngredientMarkerLimit; ++i) {
				const auto wide = L"MrAlchemyEffect" + std::to_wstring(i);
				std::string name;
				for (const wchar_t c : wide) name.push_back(static_cast<char>(c));
				auto* image = ui::FindInTree(a_row, name.c_str());
				if (image && !ui::IsWithin(image, a_row)) continue;
				const bool show = i < static_cast<int>(markers.size());
				if (!image && show) image = EffectCircle(canvas, a_row, wide.c_str());
				if (!image) continue;
				ui::Visible(image, show);
				if (!show) continue;
				const auto fill = MarkerColor(markers[i]);
				ui::RoundedBox(image, true, fill.data(), outline.data(), line);
				if (auto* slot = SlotOf(image)) {
					ui::Anchors(slot, 1.0, 0.5, 1.0, 0.5);
					ui::Vec2(slot, L"SetAlignment", 0, 0.5);
					ui::Vec2(slot, L"SetSize", g_markerDiameter, g_markerDiameter);
					ui::Vec2(slot, L"SetPosition", IngredientMarkerX(i, static_cast<int>(markers.size()), kWeightCx), g_columnDy);
				}
			}
		}

		void DecorateIngredientRow(UE::UObject* a_row)
		{
			auto* text = a_row ? RowNameText(a_row) : nullptr;
			if (!text) return;
			const auto shown = uix::GetText(text);
			DecorateIngredientMarkers(a_row, shown); // empty/recycled rows must also hide old markers
			if (shown.empty()) return;
			std::pair<float, int> stats{ -1.0f, -1 };
			if (auto it = g_formData.find(shown); it != g_formData.end()) stats = it->second;
			else if (auto m = g_formMatch.find(MatchKey(shown)); m != g_formMatch.end()) stats = m->second;
			for (int c = 0; c < 2; ++c) {
				auto* label = ui::FindInTree(a_row, c ? "MrAlchemyValue" : "MrAlchemyWeight");
				if (!label) label = ColumnLabel(a_row, text, c ? L"MrAlchemyValue" : L"MrAlchemyWeight", c ? kValueCx : kWeightCx);
				if (!label) continue;
				const auto want = c ? FormatValue(stats.second) : FormatWeight(stats.first);
				if (uix::GetText(label) != want) uix::SetText(label, want);
				if (!g_dyKnown && c == 0) {   // the numbers sat at the row's top (owner 2026-11-04): centre them on the name
					Box tb, lb;
					if (ui::Measure(text, tb.x, tb.y, tb.w, tb.h) && tb.h > 0 && ui::Measure(label, lb.x, lb.y, lb.w, lb.h) && lb.h > 0) {
						g_columnDy = ColumnDy(tb.y, tb.h, lb.y, lb.h);
						g_dyKnown = true;
					}
				}
				if (g_dyKnown && g_dyDone.insert(label).second) {
					if (auto* slot = SlotOf(label)) ui::Vec2(slot, L"SetPosition", -(c ? kValueCx : kWeightCx), g_columnDy);
				}
			}
		}

		UE::UClass* g_ingredientRowClass = nullptr;

		void OnIngredientItemBound(UE::UObject* a_row, UE::UFunction* a_fn, void*)
		{
			if (!g_open || pe::FunctionName(a_fn) != "OnListItemObjectSet" || !reflect::IsLive(a_row)) return;
			DecorateIngredientRow(a_row);
			ui::Call prepass(a_row, L"ForceLayoutPrepass");
			if (prepass) prepass.Run();
		}

		// Periodic refresh for existing rows. Rebinding also applies cosmetics before paint.
		void DecorateIngredientRows(UE::UObject* a_menu)
		{
			auto* list = InnerList(Field(a_menu, "IngredientsListView"));
			if (!list) return;
			for (auto* row : DisplayedRows(list)) {
				if (auto* cls = row->GetClass(); cls && cls != g_ingredientRowClass &&
				    pe::WatchPost(cls, &OnIngredientItemBound)) g_ingredientRowClass = cls;
				DecorateIngredientRow(row);
			}
		}

		void Layout(UE::UObject* a_menu, bool a_readNames = true)   // a_readNames false: before the list is built (RMI-021)
		{
			auto* canvas = ui::TreeRootCanvas(a_menu);
			auto* applied = Field(a_menu, "AppliedFilters_Text");
			auto* filtersButton = Field(a_menu, "Filters_Button");
			auto* listWidget = Field(a_menu, "IngredientsListView");
			g_framePlaced = false;
			if (!canvas) return;
			auto* title = reflect::Get(g_titleBox);
			if (!title) {
				title = ui::FindInTree(a_menu, "Alchemy_IngredientsListTitle");
				if (title) g_titleBox = reflect::Hold(title);
			}
			Box        t, f, lb;
			const bool haveTitle = title && ui::Measure(title, t.x, t.y, t.w, t.h) && t.w > 0;
			const bool haveFilters = filtersButton && ui::Measure(filtersButton, f.x, f.y, f.w, f.h) && f.w > 0;
			const bool haveList = listWidget && ui::Measure(listWidget, lb.x, lb.y, lb.w, lb.h);
			if (haveTitle) t.w = TextWidth(t.w, DesiredWidth(title));
			g_frameMoving = haveTitle && t.x != g_frameX;
			if (haveTitle) g_frameX = t.x;
			constexpr double kGap = 14;
			const auto  row = PlaceTitleRow(t, f, haveList ? lb.y : 0, kGap);
			static bool s_said = false;
			if (!s_said && (!haveTitle || !haveFilters || row.sortMaxWidth <= 0)) {
				s_said = true;
				logger::warn("alchemy: title row not laid out (title {} {:.0f},{:.0f} {:.0f}x{:.0f}; Filters {} {:.0f},{:.0f}; list {} y {:.0f}; sort room {:.0f})",
					haveTitle, t.x, t.y, t.w, t.h, haveFilters, f.x, f.y, haveList, lb.y, row.sortMaxWidth);
			}
			if (!haveTitle || !haveFilters) {
				HideOwn();
				return;
			}
			const double scale = ui::ViewportScale();

			// sort header: between the "Ingredients" title and the Filters button
			auto* first = reflect::Get(g_icons[0]);
			if (!first || !ui::IsWithin(first, a_menu)) {
				ResetHeader();
				g_strip = {};
				g_chips.clear();   // a new menu widget: its labels are new too
				g_chipBacks.clear();
				g_chipStyle.clear();
				g_chipText.clear();
				g_chipMarks.clear();
				g_more = {};
				g_chipTitle = {};
			}
			const auto iconRects = LayoutHeader(canvas, a_menu, scale);

			// the parchment strip above the panel; the active-filter buttons go in the space it adds
			auto* panel = Field(a_menu, "Alchemy_IngredientsListBG");
			Box   pb;
			double chipsY = row.chipsY;
			if (panel && ui::Measure(panel, pb.x, pb.y, pb.w, pb.h) && pb.w > 0) {
				auto* strip = reflect::Get(g_strip);
				if (!strip) {
					strip = MakeStrip(canvas);
					if (strip) { g_strip = reflect::Hold(strip); g_stripGreyed = -1; }
				}
				if (strip) {
					const auto st = PlaceStrip(pb, kStripAspect, kStripDx, kStripDy, kStripDw);
					if (auto* slot = SlotOf(strip)) {
						ui::Vec2(slot, L"SetPosition", st.box.x, st.box.y);
						ui::Vec2(slot, L"SetSize", st.box.w, st.box.h);
					}
					ui::Visible(strip, true);
					chipsY = st.chipsY + kChipsDy;
				}
			}

			// active filters: in the strip's space (else their own line under the title row)
			const auto names = a_readNames ? SelectedNames(a_menu) : std::vector<std::string>{};
			g_markerNames.clear();
			for (auto& h : g_chipCircles) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
			std::vector<std::string> texts, meaning;
			if (!names.empty()) {   // Clear all leftmost (owner 2026-11-04), then one entry per filter
				texts.push_back("Clear all");
				meaning.push_back("");
			}
			for (const auto& n : names) {
				texts.push_back(ChipLabel(n));   // the red x mark (U+00D7) is its own label
				meaning.push_back(n);
			}
			if (applied) ui::Visible(applied, names.empty());
			while (g_chips.size() < texts.size()) {
				auto* back = MakeBack(canvas, g_chips.size());
				auto* chip = MakeLabel(canvas, (L"MrAlchemyChip" + std::to_wstring(g_chips.size())).c_str(), g_chips.empty() ? kChipInk : kSortInk, kChipText);
				auto* mark = MakeLabel(canvas, (L"MrAlchemyChipX" + std::to_wstring(g_chips.size())).c_str(), kChipInk, kChipText);
				if (!chip || !back || !mark) break;
				uix::SetText(mark, "\xC3\x97");
				g_chipMarks.push_back(reflect::Hold(mark));
				g_chips.push_back(reflect::Hold(chip));
				g_chipBacks.push_back(reflect::Hold(back));
				g_chipStyle.push_back(-1);
				g_chipText.emplace_back();
			}
			auto* more = reflect::Get(g_more);
			if (!more) {
				more = MakeLabel(canvas, L"MrAlchemyChipMore", kChipInk, kChipText);
				if (more) g_more = reflect::Hold(more);
			}
			std::vector<Rect> rects(texts.size());   // an empty rect is not clickable
			std::vector<bool> shown(g_chips.size(), false);
			bool              moreShown = false;
			if (!texts.empty() && g_chips.size() >= texts.size()) {
				// each chip keeps its own text; a width is used once the label has drawn that text (else an estimate)
				std::vector<double>                 widths(texts.size());
				std::vector<std::array<double, 2>> sizes(texts.size());
				for (std::size_t i = 0; i < texts.size(); ++i) {
					auto* chip = reflect::Get(g_chips[i]);
					if (!chip) continue;
					if (g_chipText[i] != texts[i]) {
						uix::SetText(chip, texts[i]);
						g_chipText[i] = texts[i];
					} else {
						sizes[i] = DesiredSize(chip);
						if (sizes[i][0] > 0) g_textWidth[texts[i]] = sizes[i][0];
					}
					const auto known = g_textWidth.find(texts[i]);
					const double w = known != g_textWidth.end() ? known->second : 10.0 * static_cast<double>(texts[i].size());
					if (sizes[i][1] <= 0) sizes[i] = { w, 22.0 };
					widths[i] = w + kButtonPadX * 2 + (i > 0 ? MarkWidth() + kMarkGap : 0.0) + (i > 0 && i <= kMarkerLimit ? g_markerDiameter + kMarkerGap : 0);
				}
				const std::string moreText = "+" + std::to_string(texts.size());   // widest marker text it can need
				if (more) {
					const auto it = g_textWidth.find(moreText);
					if (it == g_textWidth.end()) {
						uix::SetText(more, moreText);
						const double mw = DesiredWidth(more);
						if (mw > 0) g_textWidth[moreText] = mw;
					}
				}
				const auto   mw = g_textWidth.count(moreText) ? g_textWidth[moreText] : 30.0;
				// two rows (owner 2026-11-04): Clear all leftmost on the first, the rest wraps under it, "+N" last
				const auto   flow = FlowButtons(widths, row.chipsRight - row.chipsX, kGap, mw, kChipRows);
				double       lineH = 0;
				for (const auto& sz : sizes) lineH = (std::max)(lineH, sz[1] + kButtonPadY * 2);
				const double step = lineH + kRowGap;
				const double x0 = row.chipsX + kButtonPadX;
				const int    hovered = g_hoverChip.load(), padFocus = g_chipFocus.load();
				for (std::size_t i = 0; i < flow.row.size(); ++i) {
					auto* chip = reflect::Get(g_chips[i]);
					auto* back = reflect::Get(g_chipBacks[i]);
					if (!chip || !back) break;
					const double x = x0 + flow.x[i];
					const double y = chipsY + step * flow.row[i];
					auto* mark = i < g_chipMarks.size() ? reflect::Get(g_chipMarks[i]) : nullptr;
					if (i > 0 && mark) {
						Place(mark, x, y, 0.0);
						Place(chip, x + MarkWidth() + kMarkGap, y, 0.0);
					} else {
						Place(chip, x, y, 0.0);
					}
					if (mark) ui::Visible(mark, i > 0);
					if (i > 0 && i <= kMarkerLimit) {
						auto* circle = EffectCircle(canvas, a_menu, (L"MrAlchemyChipColor" + std::to_wstring(i - 1)).c_str());
						if (circle) {
							g_chipCircles[i - 1] = reflect::Hold(circle);
							const auto fill = MarkerColor(static_cast<int>(i - 1)), outline = MarkerOutline();
							ui::RoundedBox(circle, true, fill.data(), outline.data(), static_cast<float>(1.0 / (std::max)(0.01, scale)));
							if (auto* slot = SlotOf(circle)) {
								ui::Vec2(slot, L"SetAlignment", 0, 0.5);
								ui::Vec2(slot, L"SetPosition", x + widths[i] - kButtonPadX * 2 - g_markerDiameter, y);
								ui::Vec2(slot, L"SetSize", g_markerDiameter, g_markerDiameter);
							}
							ui::Visible(circle, true);
						}
						g_markerNames.push_back(names[i - 1]);
					}
					const auto box = ButtonBox(x, y, widths[i] - kButtonPadX * 2, sizes[i][1], kButtonPadX, kButtonPadY);
					if (auto* slot = SlotOf(back)) {
						ui::Vec2(slot, L"SetPosition", box.x, box.y);
						ui::Vec2(slot, L"SetSize", box.w, box.h);
					}
					StyleBack(i, static_cast<int>(i) == hovered || static_cast<int>(i) == padFocus);
					ui::Visible(back, true);
					shown[i] = true;
					rects[i] = PixelRect(back, scale);
				}
				if (flow.more && more) {   // "+N": the filters that did not fit (not a button)
					uix::SetText(more, "+" + std::to_string(texts.size() - flow.row.size()));
					Place(more, x0 + flow.moreX - kButtonPadX + kGap * 0.5, chipsY + step * flow.moreRow, 0.0);
					moreShown = true;
				}
			}
			if (more) ui::Visible(more, moreShown);
			auto* chipTitle = reflect::Get(g_chipTitle);
			if (!chipTitle) {
				chipTitle = MakeLabel(canvas, L"MrAlchemyChipTitle", kTitleInk, 21);
				if (chipTitle) {
					uix::SetText(chipTitle, "Selected Filters");
					CopyIngredientsFont(chipTitle, title);
					g_chipTitle = reflect::Hold(chipTitle);
				}
			}
			if (chipTitle) {
				Place(chipTitle, row.chipsX, chipsY - kTitleAbove, 0.0);
				ui::Visible(chipTitle, !texts.empty());
			}
			for (std::size_t k = 0; k < g_chips.size(); ++k) {
				if (auto* chip = reflect::Get(g_chips[k])) ui::Visible(chip, shown[k]);
				if (!shown[k] && k < g_chipMarks.size()) {
					if (auto* mark = reflect::Get(g_chipMarks[k])) ui::Visible(mark, false);
				}
				if (!shown[k]) {
					if (auto* back = k < g_chipBacks.size() ? reflect::Get(g_chipBacks[k]) : nullptr) ui::Visible(back, false);
				}
			}
			g_chipNames = meaning;
			int shownCount = 0;
			for (std::size_t k = 0; k < shown.size(); ++k) shownCount += shown[k] ? 1 : 0;
			g_chipCount = shownCount;
			if (const int focus = g_chipFocus.load(); focus >= 0) {   // a removed filter shrank the line
				const int clamped = ClampFocus(focus, shownCount);
				if (clamped != focus) { g_chipFocus = clamped; g_hoverNow = true; }
			}
			std::scoped_lock lock(g_rectLock);
			g_iconRects = iconRects;
			g_chipRects = rects;
			g_framePlaced = true;
		}

		// ---- controller hints (owner 2026-11-04; KB game-internals "Alchemy footer (bottom hint bar)")
		bool PadInput()   // the game's own idea of the input device: ECommonInputType 1 = Gamepad
		{
			auto* sub = reflect::Get(g_inputSubsystem);
			if (!sub) {
				static ULONGLONG s_next = 0;
				if (GetTickCount64() < s_next) return false;
				s_next = GetTickCount64() + 2000;   // an object scan: at most every 2 s until found
				auto* cls = ui::Class(L"/Script/CommonInput.CommonInputSubsystem");
				for (auto* o : cls ? reflect::Instances(cls) : std::vector<UE::UObject*>{}) {
					if (!reflect::IsLive(o) || pe::Utf8(o->GetFName().ToString()).rfind("Default__", 0) == 0) continue;
					sub = o;
					g_inputSubsystem = reflect::Hold(o);
					break;
				}
				if (!sub) return false;
			}
			ui::Call type(sub, L"GetCurrentInputType");
			return type && type.Run() && type.Get<std::uint8_t>("ReturnValue") == 1;
		}

		std::vector<UE::UObject*> PanelChildren(UE::UObject* a_panel)
		{
			std::vector<UE::UObject*> out;
			ui::Call count(a_panel, L"GetChildrenCount");
			const int n = count && count.Run() ? count.Get<std::int32_t>("ReturnValue") : 0;
			for (int i = 0; i < n && i < 32; ++i) {
				ui::Call at(a_panel, L"GetChildAt");
				if (!at) break;
				at.Set("Index", i);
				if (at.Run()) if (auto* w = at.Get<UE::UObject*>("ReturnValue")) out.push_back(w);
			}
			return out;
		}

		std::string ObjName(UE::UObject* a_o) { return a_o ? pe::Utf8(a_o->GetFName().ToString()) : std::string(); }

		void CopyLook(UE::UObject* a_to, UE::UObject* a_from)   // a text's font and colour
		{
			for (const char* prop : { "Font", "ColorAndOpacity" }) {
				const auto off = reflect::Offset(a_from->GetClass(), prop);
				const auto size = ui::SizeOf(a_from->GetClass(), prop);
				if (off >= 0 && size > 0 && size <= 512)
					ui::CallFirst(a_to, prop == std::string_view("Font") ? L"SetFont" : L"SetColorAndOpacity", reinterpret_cast<const std::uint8_t*>(a_from) + off, static_cast<std::size_t>(size));
			}
		}

		UE::UObject* NewWidget(UE::UObject* a_outer, const wchar_t* a_class, const std::wstring& a_name)
		{
			auto* cls = ui::Class(a_class);
			return cls && a_outer ? UE::NewObject<UE::UObject>(a_outer, cls, UE::FName(a_name.c_str())) : nullptr;
		}

		UE::UObject* AddTo(UE::UObject* a_panel, const wchar_t* a_fn, UE::UObject* a_child)
		{
			ui::Call add(a_panel, a_fn);
			if (!add) return nullptr;
			add.Set("Content", a_child);
			add.Run();
			return add.Get<UE::UObject*>("ReturnValue");
		}

		void FillSlot(UE::UObject* a_slot)   // overlay slots start top-left at the brush's own size
		{
			if (!a_slot) return;
			const std::uint8_t fill = 0;   // HAlign_Fill / VAlign_Fill
			ui::CallFirst(a_slot, L"SetHorizontalAlignment", &fill, sizeof(fill));
			ui::CallFirst(a_slot, L"SetVerticalAlignment", &fill, sizeof(fill));
		}


		double HoldTime(UE::UObject* a_entry)   // VAltarBoundActionButton.HoldTreshold (sic); 0 for a press entry
		{
			const auto off = a_entry ? reflect::Offset(a_entry->GetClass(), "HoldTreshold") : -1;
			const auto size = a_entry ? ui::SizeOf(a_entry->GetClass(), "HoldTreshold") : -1;
			if (off < 0) return 0;
			const auto* p = reinterpret_cast<const std::uint8_t*>(a_entry) + off;
			return size == 8 ? *reinterpret_cast<const double*>(p) : size == 4 ? *reinterpret_cast<const float*>(p) : 0.0;
		}

		double GlyphSide(UE::UObject* a_entry)   // the native glyph's height, else kHintGlyph
		{
			auto* glyph = Field(a_entry, "InputActionWidget");
			ui::Call size(glyph, L"GetDesiredSize");
			if (glyph && size && size.Run()) {
				const auto d = size.Get<std::array<double, 2>>("ReturnValue");
				if (d[1] > 4 && d[1] < 120) return d[1];
			}
			return kHintGlyph;
		}

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

		UE::UObject* MakeHint(UE::UObject* a_box, int a_index, UE::UObject* a_native, UE::UObject* a_holdEntry, UE::UObject* a_pressEntry)
		{
			auto* outer = a_box->GetOuter();
			const auto id = std::to_wstring(a_index);
			auto* row = NewWidget(outer, L"/Script/UMG.HorizontalBox", L"MrAlchemyHint" + id);
			auto* sizer = NewWidget(outer, L"/Script/UMG.SizeBox", L"MrAlchemyHintSize" + id);
			auto* stack = NewWidget(outer, L"/Script/UMG.Overlay", L"MrAlchemyHintStack" + id);
			auto* glyph = NewWidget(outer, L"/Script/UMG.Image", L"MrAlchemyHintGlyph" + id);
			auto* label = NewWidget(outer, L"/Script/UMG.TextBlock", L"MrAlchemyHintText" + id);
			if (!row || !sizer || !stack || !glyph || !label) return nullptr;
			const auto&  hint = kHints[a_index];
			const double press = GlyphSide(a_pressEntry ? a_pressEntry : a_native);   // the glyph itself
			const double side = hint.hold && a_holdEntry ? (std::max)(press, GlyphSide(a_holdEntry)) : press;   // with the ring
			ui::Float(sizer, L"SetWidthOverride", static_cast<float>(side));
			ui::Float(sizer, L"SetHeightOverride", static_cast<float>(side));
			if (hint.hold) {   // the native hold ring (progress brush) behind the glyph - only when it has a picture
				auto* ringSource = Field(a_holdEntry, "InputActionWidget");
				const auto off = ringSource ? reflect::Offset(ringSource->GetClass(), "ProgressMaterialBrush") : -1;
				const auto size = ringSource ? ui::SizeOf(ringSource->GetClass(), "ProgressMaterialBrush") : -1;
				if (off >= 0 && size > 0 && size <= 1024) {
					if (auto* ring = NewWidget(outer, L"/Script/UMG.Image", L"MrAlchemyHintRing" + id)) {
						ui::CallFirst(ring, L"SetBrush", reinterpret_cast<const std::uint8_t*>(ringSource) + off, static_cast<std::size_t>(size));
						if (ui::BrushResource(ring)) FillSlot(AddTo(stack, L"AddChildToOverlay", ring));   // the rim brush drew a white box
					}
				}
			}
			SetTexture(glyph, NativeTexture(hint.glyph));
			auto* inner = NewWidget(outer, L"/Script/UMG.SizeBox", L"MrAlchemyHintInner" + id);   // the glyph at the press size, centred
			if (inner) {
				ui::Float(inner, L"SetWidthOverride", static_cast<float>(press));
				ui::Float(inner, L"SetHeightOverride", static_cast<float>(press));
				ui::Call innerContent(inner, L"SetContent");
				if (innerContent) { innerContent.Set("Content", glyph); innerContent.Run(); }
				if (auto* slot = AddTo(stack, L"AddChildToOverlay", inner)) {
					const std::uint8_t centre = 2;   // HAlign_Center / VAlign_Center
					ui::CallFirst(slot, L"SetHorizontalAlignment", &centre, sizeof(centre));
					ui::CallFirst(slot, L"SetVerticalAlignment", &centre, sizeof(centre));
				}
			} else {
				FillSlot(AddTo(stack, L"AddChildToOverlay", glyph));
			}
			ui::Call content(sizer, L"SetContent");
			if (content) { content.Set("Content", stack); content.Run(); }
			const std::uint8_t centre = 2;   // EVerticalAlignment::VAlign_Center
			if (auto* slot = AddTo(row, L"AddChildToHorizontalBox", sizer)) ui::CallFirst(slot, L"SetVerticalAlignment", &centre, sizeof(centre));
			if (hint.label) uix::SetText(label, hint.label);   // mirrored labels are set by LayoutFooter
			if (auto* nativeText = Field(a_native, "Text_ActionName")) CopyLook(label, nativeText);
			if (auto* slot = AddTo(row, L"AddChildToHorizontalBox", label)) {
				ui::CallFirst(slot, L"SetVerticalAlignment", &centre, sizeof(centre));
				const float pad[4] = { static_cast<float>(kHintGap), 0, 0, 0 };
				ui::CallFirst(slot, L"SetPadding", pad, sizeof(pad));
			}
			if (auto* slot = AddTo(a_box, L"AddChildToHorizontalBox", row)) {   // the native slots have no padding (screenshot)
				ui::CallFirst(slot, L"SetVerticalAlignment", &centre, sizeof(centre));
				const float pad[4] = { a_index == 0 ? 0.0f : static_cast<float>(kHintSpacing), 0, 0, 0 };
				ui::CallFirst(slot, L"SetPadding", pad, sizeof(pad));
			}
			return row;
		}

		// On a pad the own bar mirrors native Y Filters / hold Y Clear all and adds RB Sort / LB Selected Filters.
		// The footer rebuilds its entries on every device or panel change (KB): this runs from the tick and re-applies.
		// our own bar beside BA_Box in its parent, placed like it (same slot alignment and padding); created once per footer
		UE::UObject* OwnBar(UE::UObject* a_footer, UE::UObject* a_box)
		{
			if (auto* bar = ui::FindInTree(a_footer, "MrAlchemyBar")) return bar;
			ui::Call parentOf(a_box, L"GetParent");
			auto* parent = parentOf && parentOf.Run() ? parentOf.Get<UE::UObject*>("ReturnValue") : nullptr;
			auto* bar = parent ? NewWidget(a_box->GetOuter(), L"/Script/UMG.HorizontalBox", L"MrAlchemyBar") : nullptr;
			if (!bar) return nullptr;
			auto* slot = AddTo(parent, L"AddChildToOverlay", bar);
			if (!slot) slot = AddTo(parent, L"AddChildToHorizontalBox", bar);
			if (!slot) slot = AddTo(parent, L"AddChildToVerticalBox", bar);
			auto* boxSlot = SlotOf(a_box);
			if (slot && boxSlot) {
				for (const char* prop : { "HorizontalAlignment", "VerticalAlignment" }) {
					const auto off = reflect::Offset(boxSlot->GetClass(), prop);
					if (off >= 0) {
						const std::uint8_t v = *(reinterpret_cast<const std::uint8_t*>(boxSlot) + off);
						ui::CallFirst(slot, prop == std::string_view("HorizontalAlignment") ? L"SetHorizontalAlignment" : L"SetVerticalAlignment", &v, sizeof(v));
					}
				}
				const auto off = reflect::Offset(boxSlot->GetClass(), "Padding");
				if (off >= 0 && ui::SizeOf(boxSlot->GetClass(), "Padding") == 16)
					ui::CallFirst(slot, L"SetPadding", reinterpret_cast<const std::uint8_t*>(boxSlot) + off, 16);
			}
			return bar;
		}

		// On a pad with the Filters panel closed our bar (owner's order) replaces the game's: BA_Box is faded out, never
		// changed - the game keeps deciding which of its entries show on each device (forcing them visible showed the
		// keyboard-hidden ones, owner screenshot on fa3e9a32). Runs from the tick; the footer rebuilds its entries.
		void LayoutFooter(UE::UObject* a_menu, bool a_filtersOpen, bool a_pad)
		{
			auto* footer = Field(a_menu, "modern_footer");
			auto* box = footer ? ui::FindInTree(footer, "BA_Box") : nullptr;
			if (!box) return;
			const bool                show = a_pad;
			UE::UObject*              native = nullptr;
			UE::UObject*              holdEntry = nullptr;
			std::vector<UE::UObject*> press;   // the game's press entries in its order (A Add to Potion, B Back)
			for (auto* child : PanelChildren(box)) {
				if (!Field(child, "Text_ActionName")) continue;
				if (!native) native = child;
				// the hold-Y entry (game's Clear All Filters) by its hold time; the action name as a fallback
				auto*      action = Field(child, "BoundInputAction");
				const bool hold = HoldTime(child) > 0 || (action && ObjName(action).find("ClearAll") != std::string::npos);
				if (hold && !holdEntry) holdEntry = child;
				else if (!hold) press.push_back(child);
			}
			ui::Float(box, L"SetRenderOpacity", show ? 0.0f : 1.0f);
			auto* bar = show && native ? OwnBar(footer, box) : ui::FindInTree(footer, "MrAlchemyBar");
			if (!bar) return;
			ui::Visible(bar, show);
			if (!show) return;
			std::array<UE::UObject*, std::size(kHints)> own{};
			for (auto* child : PanelChildren(bar)) {
				const auto name = ObjName(child);
				if (name.rfind("MrAlchemyHint", 0) != 0) continue;
				const auto i = static_cast<std::size_t>(name.back() - '0');
				if (i < own.size()) own[i] = child;
			}
			UE::UObject* pressEntry = press.empty() ? nullptr : press.front();
			for (std::size_t i = 0; i < own.size(); ++i) {
				if (!own[i]) own[i] = MakeHint(bar, static_cast<int>(i), native, holdEntry, pressEntry);
				if (!own[i]) continue;
				const bool hintShown = (!a_filtersOpen || i == 0 || i == 2 || i == 5 || i == 6) &&
					(i != 6 || (searchcfg::Get().alchemy && kbcfg::Get().enabled));
				ui::Visible(own[i], hintShown);
				const int mirror = kHints[i].mirror;
				if (mirror < 0) continue;
				auto* source = mirror < static_cast<int>(press.size()) ? Field(press[static_cast<std::size_t>(mirror)], "Text_ActionName") : nullptr;
				auto* label = ui::FindInTree(footer, "MrAlchemyHintText" + std::to_string(i));
				if (source && label) {
					const auto want = uix::GetText(source);
					if (!want.empty() && uix::GetText(label) != want) uix::SetText(label, want);
				}
			}
		}


		// ---- the menu
		int MoveChipFocus(int a_index, pickerpad::Direction a_direction)
		{
			const int dx = a_direction == pickerpad::Direction::Left ? -1 : a_direction == pickerpad::Direction::Right ? 1 : 0;
			const int dy = a_direction == pickerpad::Direction::Up ? -1 : a_direction == pickerpad::Direction::Down ? 1 : 0;
			std::scoped_lock lock(g_rectLock);
			return ChipStep(g_chipRects, a_index, dx, dy, g_chipCount.load());
		}

		void ApplySortInput()
		{
			if (const int icon = g_iconClick.exchange(-1); icon >= 0 && icon < kIcons) {
				g_sortState = Click(g_sortState, static_cast<SortKey>(icon));
				g_dirtySort = true;
				g_nextLayout = {};
			}
			if (const int direction = g_sortDirection.exchange(-1); direction >= 0 && direction < kIcons * 2) {
				g_sortState = { static_cast<SortKey>(direction / 2), (direction & 1) != 0 };
				g_dirtySort = true;
				g_nextLayout = {};
			}
		}

		bool Active(UE::UObject* a_menu) { return a_menu && Flag(a_menu, "bIsActive"); }

		// Order the native getter's independent result before the game builds row widgets.
		reflect::Handle g_ingredientSourceVM;
		reflect::Handle g_ingredientEntryWrapper;

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
				if (!ReadIngredient(result.data + i * stride, ingredients[i], true, false)) return;
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
		a_y = ly - cy - (a_filters ? 0 : gap) - kSearchSpace;
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
		if (auto* strip = reflect::Get(g_strip)) {   // grey with the Ingredients panel while the Filters panel is open
			auto*     filters = Field(menu, "Alchemy_Filters");
			const int greyed = filters && Active(filters) ? 1 : 0;
			if (greyed != g_stripGreyed) {
				const float t = StripTint(greyed != 0);
				ui::Colour(strip, t, t, t, 1.0f);
				g_stripGreyed = greyed;
			}
		}
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

	bool Message(UINT a_message, WPARAM a_key, LPARAM a_position)
	{
		if (!g_openFlag.load() || !Enabled() || search::Typing() || keyboard::Taking()) return false;
		if (g_filtersVisible.load() && a_message == WM_KEYDOWN && (a_key == 'A' || a_key == 'D')) {
			if (!(a_position & (1 << 30))) g_tabCycle = a_key == 'A' ? -1 : 1;
			return true;
		}
		if (a_message == WM_LBUTTONDOWN) {
			const double x = static_cast<short>(LOWORD(a_position)), y = static_cast<short>(HIWORD(a_position));
			std::scoped_lock lock(g_rectLock);
			if (g_filtersVisible.load()) {
				for (int i = 0; i < 3; ++i) if (g_tabRects[i].Has(x, y)) {
					g_tabRequest = i;
					g_swallowUp = true;
					return true;
				}
			}
			for (int i = 0; i < kIcons; ++i) if (g_iconRects[static_cast<std::size_t>(i)].Has(x, y)) {
				g_iconClick = i;
				g_swallowUp = true;
				return true;
			}
			for (std::size_t i = 0; i < g_chipRects.size(); ++i) {
				if (g_chipRects[i].Has(x, y)) {
					g_chipClick = static_cast<int>(i);
					g_swallowUp = true;
					return true;
				}
			}
			return false;
		}
		if (a_message == WM_MOUSEMOVE) {
			const double     x = static_cast<short>(LOWORD(a_position)), y = static_cast<short>(HIWORD(a_position));
			std::vector<Box> boxes;
			{
				std::scoped_lock lock(g_rectLock);
				for (const auto& r : g_chipRects) boxes.push_back(r.ok ? Box{ r.x, r.y, r.w, r.h } : Box{});
			}
			int tabHit = -1;
			{
				std::scoped_lock lock(g_rectLock);
				if (g_filtersVisible.load()) for (int i = 0; i < 3; ++i) if (g_tabRects[i].Has(x, y)) { tabHit = i; break; }
			}
			g_hoverTab = tabHit;
			int iconHit = -1;
			{
				std::scoped_lock lock(g_rectLock);
				for (int i = 0; i < kIcons; ++i) if (g_iconRects[static_cast<std::size_t>(i)].Has(x, y)) { iconHit = i; break; }
			}
			if (g_hoverIcon.exchange(iconHit) != iconHit) g_headerNow = true;
			const int hit = HitIndex(boxes, x, y);
			if (g_hoverChip.exchange(hit) != hit) g_hoverNow = true;
			return false;   // the game still sees the mouse move
		}
		if (a_message == WM_LBUTTONUP && g_swallowUp) {
			g_swallowUp = false;
			return true;
		}
		return false;   // no sort key: the header icons are clicked (owner 2026-11-04)
	}

	// Native X/Create and Y/Filters (hold Y/Clear all) pass through unchanged.
	// LB selects filter chips; RB selects sort icons. Native panels/text input own their controls.
	// Buttons consumed by a custom mode remain suppressed until released.
	void Controller(std::uint16_t& a_buttons, short& a_lx, short& a_ly)
	{
		const pickerpad::Input raw{ a_buttons, a_lx, a_ly };
		g_sortNav.Sample(raw, GetTickCount64());
		const auto filterStick = [&] {
			const auto filtered = g_sortNav.Filter(raw);
			a_lx = filtered.x; a_ly = filtered.y;
		};
		g_padSwallow &= a_buttons;
		const std::uint16_t pressed = a_buttons & static_cast<std::uint16_t>(~g_lastButtons);
		g_lastButtons = a_buttons;
		if (g_openFlag.load() && Enabled() && !g_filtersVisible.load() && !search::Typing() && !keyboard::Taking() &&
			g_padFocus.load() < 0 && g_chipFocus.load() < 0 && (pressed & kPadName) && g_canName.load()) {
			g_nameKeyboardRequest = true;
			g_padSwallow |= kPadName;
		}
		if (!g_openFlag.load() || !Enabled() || g_filtersVisible.load() || g_nameTyping.load() || search::Typing() || keyboard::Taking()) {
			if (g_padFocus.exchange(-1) >= 0) g_headerNow = true;
			if (g_chipFocus.exchange(-1) >= 0) g_hoverNow = true;
			a_buttons &= static_cast<std::uint16_t>(~g_padSwallow);
			filterStick();
			return;
		}
		if (pressed & kPadRB) {
			g_padFocus = g_padFocus.load() >= 0 ? -1 : g_activeIcon.load();
			g_chipFocus = -1;
			g_headerNow = g_hoverNow = true;
		} else if (pressed & kPadLB) {
			g_chipFocus = g_chipFocus.load() >= 0 || g_chipCount.load() == 0 ? -1 : 0;
			g_padFocus = -1;
			g_headerNow = g_hoverNow = true;
		}
		g_padSwallow |= static_cast<std::uint16_t>(a_buttons & (kPadLB | kPadRB));
		const int sortFocus = g_padFocus.load(), chipFocus = g_chipFocus.load();
		if (sortFocus >= 0 || chipFocus >= 0) {
			const auto direction = g_sortNav.pulse;
			g_sortNav.Consume(raw, 0, true);
			if (pressed & kPadB) {
				g_padFocus = g_chipFocus = -1;
				g_headerNow = g_hoverNow = true;
			} else if (sortFocus >= 0 && (direction == pickerpad::Direction::Left || direction == pickerpad::Direction::Right)) {
				g_padFocus = SortNavStep(sortFocus, direction == pickerpad::Direction::Left ? -1 : 1);
				g_headerNow = true;
			} else if (sortFocus >= 0 && (direction == pickerpad::Direction::Up || direction == pickerpad::Direction::Down)) {
				g_sortDirection = sortFocus * 2 + (direction == pickerpad::Direction::Down ? 1 : 0);
			} else if (chipFocus >= 0 && direction != pickerpad::Direction::None) {
				g_chipFocus = MoveChipFocus(chipFocus, direction);
				g_hoverNow = true;
			} else if (pressed & kPadA) {
				if (sortFocus >= 0) g_iconClick = sortFocus;
				else g_chipClick = chipFocus;
			}
			g_padSwallow |= static_cast<std::uint16_t>(a_buttons & (kPadUp | kPadDown | kPadLeft | kPadRight | kPadA | kPadB));
		}
		a_buttons &= static_cast<std::uint16_t>(~g_padSwallow);
		filterStick();
	}
}
