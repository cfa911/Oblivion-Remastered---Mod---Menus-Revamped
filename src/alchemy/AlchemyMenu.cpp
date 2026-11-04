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
#include "core/Runtime.h"
#include "engine/Menus.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/Ui.h"
#include "engine/UiText.h"
#include "sorting/rules/AlchemyRules.h"

namespace alchemy
{
	namespace
	{
		using namespace alchemyrules;
		using Clock = std::chrono::steady_clock;

		constexpr const wchar_t* kMenuClass = L"/Game/UI/Modern/GameMenuLayer/Alchemy/WBP_ModernMenu_AlchemyMenu.WBP_ModernMenu_AlchemyMenu_C";
		constexpr std::uint16_t  kLeftThumb = 0x0040;      // XINPUT_GAMEPAD_LEFT_THUMB
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
		// the parchment strip above the Ingredients panel (owner "Better Mask" PSD v4 2026-11-04: 1090x240, a soft torn edge).
		// On 2559x1439 it sits at 100,7 px, 1090 wide = 75.0, 5.25, 817.5 units; the panel background widget measured 60.8,
		// 93.7, 845.5 wide there (log 2026-11-04 10:38), so the offsets are +14.2, -88.45, -28.0.
		constexpr const wchar_t* kStripFile = L"Alchemy-TopParchment.png";
		constexpr double         kStripAspect = 240.0 / 1090.0, kStripDx = 14.2, kStripDy = -88.45, kStripDw = -28.0;

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
		SortKey         g_sort = SortKey::Name;
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
		std::pair<std::int32_t, void*> g_filterSignature{ -1, nullptr };
		Clock::time_point g_nextLayout{};
		std::unordered_map<std::string, std::pair<float, int>> g_formData;   // display name -> weight, value (-1 unknown)

		reflect::Handle              g_sortLabel, g_titleBox, g_strip, g_stripTexture;
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
		Rect              g_sortRect;
		std::vector<Rect> g_chipRects;
		std::atomic<int>  g_cycle{ 0 };
		std::atomic<int>  g_chipClick{ -1 };
		std::atomic<bool> g_openFlag{ false };
		std::atomic<bool> g_nameTyping{ false };
		bool              g_swallowUp = false;
		bool              g_swallowKeyUp = false;
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

		std::pair<float, int> FormData(const std::string& a_name, const UE::FText& a_text)
		{
			if (auto it = g_formData.find(a_name); it != g_formData.end()) return it->second;
			std::pair<float, int> data{ -1.0f, -1 };
			const auto key = reflect::TextKey(a_text);   // "LOC_FN_<EditorID>" for a normal form's name
			if (key.rfind("LOC_FN_", 0) == 0) {
				if (auto* form = RE::TESForm::LookupByEditorID<RE::IngredientItem>(key.substr(7))) data = { form->weight, form->data.costOverride };
			}
			g_formData.emplace(a_name, data);
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
			if (!NeedCounts(g_countsOpened, g_countsNum, arr->num)) return;
			g_countsOpened = false;
			g_countsNum = arr->num;
			logger::flush();
			std::vector<Ingredient> all;
			for (std::int32_t i = 0; arr->data && i < arr->num && i < 2000; ++i) {
				Ingredient item;
				if (ReadIngredient(arr->data + static_cast<std::ptrdiff_t>(i) * l.size, item, false)) all.push_back(std::move(item));
			}
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
					if (g_itemNames < 0) logger::warn("alchemy: no effect name found in the filter item (size {:X}, {} texts, row \"{}\"); one-click removal and hiding empty effects are off", size, texts.size(), shown);
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

		void DecorateRows(UE::UObject* a_filters, const std::vector<std::string>& a_active)
		{
			auto* list = InnerList(Field(a_filters, "ListView_Filters"));
			if (!list) return;
			const auto rows = DisplayedRows(list);
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
				if (g_itemNames < 0) logger::warn("alchemy: no effect name found in {} filter items against {} rows; one-click removal and hiding empty effects are off", perItem.size(), rowTexts.size());
			}
			if (!mrconfig::Get().alchemy.hideZeroCountEffects || g_itemNames != 1 || !g_countsReady) return;
			auto items = ListItems(list);
			const std::pair<std::int32_t, void*> signature{ static_cast<std::int32_t>(items.size()), items.empty() ? nullptr : items.front() };
			if (signature == g_filterSignature) return;
			std::vector<UE::UObject*> keep;
			for (auto* item : items) {
				std::string name;
				if (!EffectItemName(item, name) || CountOf(name) > 0) keep.push_back(item);
			}
			if (!keep.empty() && keep.size() < items.size()) SetItems(list, keep);
			g_filterSignature = { static_cast<std::int32_t>(keep.size()), keep.empty() ? nullptr : keep.front() };
		}

		// ---- IMP-MR-005: sort
		void ApplySort(UE::UObject* a_menu)
		{
			g_dirtySort = false;
			auto* list = InnerList(Field(a_menu, "IngredientsListView"));
			if (!list) return;
			auto items = ListItems(list);
			if (items.size() < 2) return;
			const auto propsOff = reflect::Offset(items.front()->GetClass(), "Properties");
			if (propsOff < 0) return;
			std::vector<Ingredient> data(items.size());
			const bool effects = SortReadsEffects(g_sort);
			logger::flush();
			for (std::size_t i = 0; i < items.size(); ++i) {
				if (!ReadIngredient(reinterpret_cast<const std::uint8_t*>(items[i]) + propsOff, data[i], true, effects)) return;   // all or nothing
			}
			static bool s_warned = false;
			if (!s_warned && data.size() >= 5 && std::none_of(data.begin(), data.end(), [](const Ingredient& i) { return i.weight >= 0; })) {
				s_warned = true;
				logger::warn("alchemy: no ingredient weight/value found (name key lookup); those sorts fall back to the name");
			}
			const auto order = SortOrder(data, g_sort);
			bool same = true;
			for (std::size_t i = 0; i < order.size(); ++i) same &= order[i] == static_cast<int>(i);
			if (same) return;
			std::vector<UE::UObject*> sorted;
			sorted.reserve(order.size());
			for (int i : order) sorted.push_back(items[static_cast<std::size_t>(i)]);
			UE::UObject* selected = nullptr;
			{
				ui::Call get(list, L"BP_GetSelectedItem");
				if (get && get.Run()) selected = get.Get<UE::UObject*>("ReturnValue");
			}
			SetItems(list, sorted);
			if (selected) {
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
			if (!item && ClickTab(filters, !positive)) {
				switched = true;
				item = FindFilterItem(list, key);
			}
			if (item) ui::CallFirst(filters, click.c_str(), &item, sizeof(item));
			if (switched) ClickTab(filters, positive);
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
			if (ui::RoundedBox(back, false, a_hovered ? kButtonHover : kButtonFill, line, 1.5f, 6.0f)) g_chipStyle[a_index] = want;
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
			if (auto* l = reflect::Get(g_sortLabel)) ui::Visible(l, false);
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
			g_hoverChip = -1;
			std::scoped_lock lock(g_rectLock);
			g_sortRect = {};
			g_chipRects.clear();
		}

		void Layout(UE::UObject* a_menu)
		{
			auto* canvas = ui::TreeRootCanvas(a_menu);
			auto* applied = Field(a_menu, "AppliedFilters_Text");
			auto* filtersButton = Field(a_menu, "Filters_Button");
			auto* listWidget = Field(a_menu, "IngredientsListView");
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

			// sort label: between the "Ingredients" title and the Filters button
			auto* sort = reflect::Get(g_sortLabel);
			if (!sort || !ui::IsWithin(sort, a_menu)) {
				g_strip = {};
				g_chips.clear();   // a new menu widget: its labels are new too
				g_chipBacks.clear();
				g_chipStyle.clear();
				g_chipText.clear();
				g_chipMarks.clear();
				g_more = {};
				g_chipTitle = {};
				sort = MakeLabel(canvas, L"MrAlchemySort", kSortInk, 20);
				g_sortLabel = sort ? reflect::Hold(sort) : reflect::Handle{};
			}
			if (sort) {
				uix::SetText(sort, std::string("Sort: ") + SortLabel(g_sort));
				Place(sort, row.sortX, row.sortY, 0.0);
				ui::Visible(sort, row.sortMaxWidth > 0);
			}

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
			const auto names = SelectedNames(a_menu);
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
					widths[i] = w + kButtonPadX * 2 + (i > 0 ? MarkWidth() + kMarkGap : 0.0);
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
				const int    hovered = g_hoverChip.load();
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
					const auto box = ButtonBox(x, y, widths[i] - kButtonPadX * 2, sizes[i][1], kButtonPadX, kButtonPadY);
					if (auto* slot = SlotOf(back)) {
						ui::Vec2(slot, L"SetPosition", box.x, box.y);
						ui::Vec2(slot, L"SetSize", box.w, box.h);
					}
					StyleBack(i, static_cast<int>(i) == hovered);
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
					uix::SetText(chipTitle, "Selected filters");
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
			std::scoped_lock lock(g_rectLock);
			g_sortRect = sort && row.sortMaxWidth > 0 ? PixelRect(sort, scale) : Rect{};
			g_chipRects = rects;
		}

		// ---- the menu
		bool Active(UE::UObject* a_menu) { return a_menu && Flag(a_menu, "bIsActive"); }

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
				               n == "Destruct"                                            ? 4 : 0;
				it = s_kind.emplace(a_fn, k).first;
			}
			if (it->second == 4) { g_menu = {}; return; }
			if (reflect::Get(g_menu) != a_obj && reflect::IsLive(a_obj)) g_menu = reflect::Hold(a_obj);
			if (it->second == 1) g_state.ListBuilt();
			if (it->second == 1 && !g_inside && g_open) {   // the game rebuilt the ingredient list: sort it on the next tick -
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

	void Tick()
	{
		if (!Enabled() || !reflect::Ok()) return;
		auto*      menu = Menu();
		const bool open = menu && Active(menu);
		if (open != g_open) {
			g_open = open;
			g_openFlag = open;
			if (open) {
				g_state.Opened(NowMs());
				g_countsOpened = true;
				g_sort = SortFromIni(mrconfig::Get().alchemy.defaultSort);
				g_dirtySort = g_dirtyCounts = true;
				g_filterSignature = { -1, nullptr };
				g_nextLayout = {};
			} else {
				g_state.Closed();
				HideOwn();
				g_nameTyping = false;
			}
		}
		if (!open) return;
		if (g_cycle.exchange(0) > 0) {
			g_sort = NextSort(g_sort);
			g_dirtySort = true;
			g_nextLayout = {};
		}
		if (const int chip = g_chipClick.exchange(-1); chip >= 0 && chip < static_cast<int>(g_chipNames.size())) {
			const auto meaning = g_chipNames[static_cast<std::size_t>(chip)];
			const auto before = SelectedNames(menu).size();
			if (meaning.empty()) {   // Clear all: each filter off as a single entry does (the menu's input-event stub does nothing when called)
				for (const auto& name : SelectedNames(menu)) RemoveFilter(menu, name);
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
		if (!g_state.CanRead(NowMs(), kSettleMs)) return;
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
			const int hovered = g_hoverChip.load();
			for (std::size_t i = 0; i < g_chipBacks.size(); ++i) StyleBack(i, static_cast<int>(i) == hovered);
		}
		const auto now = Clock::now();
		if (now < g_nextLayout) return;
		g_nextLayout = now + std::chrono::milliseconds(100);
		if (auto* filters = Field(menu, "Alchemy_Filters"); filters && Active(filters)) DecorateRows(filters, SelectedNames(menu));
		Layout(menu);
		auto* name = Field(menu, "Alchemy_NameBox");
		ui::Call focus(name, L"HasFocusedDescendants");
		g_nameTyping = focus && focus.Run() && focus.Get<bool>("ReturnValue");
	}

	bool Message(UINT a_message, WPARAM a_key, LPARAM a_position)
	{
		if (!g_openFlag.load() || !Enabled()) return false;
		if (a_message == WM_LBUTTONDOWN) {
			const double x = static_cast<short>(LOWORD(a_position)), y = static_cast<short>(HIWORD(a_position));
			std::scoped_lock lock(g_rectLock);
			if (g_sortRect.Has(x, y)) {
				++g_cycle;
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
			const int hit = HitIndex(boxes, x, y);
			if (g_hoverChip.exchange(hit) != hit) g_hoverNow = true;
			return false;   // the game still sees the mouse move
		}
		if (a_message == WM_LBUTTONUP && g_swallowUp) {
			g_swallowUp = false;
			return true;
		}
		const bool typing = g_nameTyping.load() || core::ControllerKeyboardOpen();
		if (a_message == WM_KEYDOWN && a_key == 'S' && !typing) {
			if (!(a_position & (1 << 30))) ++g_cycle;   // not an auto-repeat
			g_swallowKeyUp = true;
			return true;
		}
		if ((a_message == WM_KEYUP && a_key == 'S' && g_swallowKeyUp)) {
			g_swallowKeyUp = false;
			return true;
		}
		if (a_message == WM_CHAR && (a_key == 's' || a_key == 'S') && !typing) return true;
		return false;
	}

	void Controller(std::uint16_t& a_buttons)
	{
		if (!g_openFlag.load() || !Enabled()) { g_lastButtons = a_buttons; return; }
		if ((a_buttons & kLeftThumb) && !(g_lastButtons & kLeftThumb)) ++g_cycle;
		g_lastButtons = a_buttons;
		a_buttons &= static_cast<std::uint16_t>(~kLeftThumb);   // ours while the Alchemy menu is open
	}
}
