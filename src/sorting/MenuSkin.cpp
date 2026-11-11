// The native menu skin passes (see MenuSkin.h). Logic only: every colour and art file comes from uistyle::Skin.
#include "core/DarkPack.h"
#include "sorting/MenuSkin.h"
#include "sorting/rules/DetailsSkin.h"
#include "sorting/rules/InventoryHeaderLayout.h"
#include "core/Config.h"
#include "engine/Reflect.h"
#include "engine/PEHook.h"
#include "engine/UiText.h"
#include "engine/DataTableRows.h"
#include <format>
#include <set>
namespace menuskin
{
	const uistyle::Skin& Current() { return uistyle::SkinFor(mrconfig::Get().stylePreset); }

	namespace
	{
		bool ReadColor(UE::UObject* widget, float* rgba)
		{
			const int offset = reflect::Offset(widget->GetClass(), "ColorAndOpacity");
			const auto* at = offset >= 0 ? reflect::At<float>(widget, offset) : nullptr;
			if (!at) return false;
			std::copy_n(at, 4, rgba);
			return true;
		}

		// A brush's own tint (FSlateBrush::TintColor.SpecifiedColor). A colour-only image draws this colour times
		// ColorAndOpacity; every census so far read only ColorAndOpacity (white), so a brown brush tint stayed invisible.
		bool ReadBrushTint(UE::UObject* widget, float* rgba)
		{
			static auto* brushStruct = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.SlateBrush");
			static auto* colorStruct = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.SlateColor");
			if (!widget || !brushStruct || !colorStruct) return false;
			static const int tint = reflect::Offset(brushStruct, "TintColor");
			static const int spec = reflect::Offset(colorStruct, "SpecifiedColor");
			const int brush = reflect::Offset(widget->GetClass(), "Brush");
			if (brush < 0 || tint < 0 || spec < 0) return false;
			const auto* at = reflect::At<float>(widget, brush + tint + spec);
			if (!at) return false;
			std::copy_n(at, 4, rgba);
			return true;
		}

		// writes the brush's own tint (UImage hands Slate a pointer to its Brush, so the write is live)
		bool WriteBrushTint(UE::UObject* widget, const float* rgba)
		{
			struct SlateColor
			{
				float rgba[4];
				std::uint8_t rule;
				std::uint8_t padding[7];
			} tintColor{ { rgba[0], rgba[1], rgba[2], rgba[3] }, 0, {} };
			ui::Call set(widget, L"SetBrushTintColor");
			if (set.Size("TintColor") == sizeof(tintColor) && set.Set("TintColor", tintColor) && set.RunGuarded()) return true;
			static auto* brushStruct = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.SlateBrush");
			static auto* colorStruct = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.SlateColor");
			if (!widget || !brushStruct || !colorStruct) return false;
			static const int tint = reflect::Offset(brushStruct, "TintColor");
			static const int spec = reflect::Offset(colorStruct, "SpecifiedColor");
			const int brush = reflect::Offset(widget->GetClass(), "Brush");
			if (brush < 0 || tint < 0 || spec < 0) return false;
			auto* at = reflect::At<float>(widget, brush + tint + spec);
			if (!at) return false;
			std::copy_n(rgba, 4, at);
			return true;
		}

		std::array<float, 4> WithAlpha(const uistyle::Color& c, float alpha) { return { c[0], c[1], c[2], alpha }; }

		std::string NameOf(UE::UObject* object) { return object ? pe::Utf8(object->GetFName().ToString()) : std::string("-"); }

		UE::UObject* SkinArt(const wchar_t* file)
		{
			static std::unordered_map<std::wstring, reflect::Handle> loaded;
			if (!file) return nullptr;
			auto it = loaded.find(file);
			if (it != loaded.end()) {
				if (auto* texture = reflect::Get(it->second)) return texture;
				if (!it->second.ptr) return nullptr;   // a missing file is reported once per run
				loaded.erase(it);                      // a previous menu and its textures may have been collected
			}
			const auto path = mrconfig::PluginFolder() / L"MenusRevamped" / L"Skin" / file;
			auto* texture = ui::ImportTexture(path);
			loaded.emplace(file, texture ? reflect::Hold(texture) : reflect::Handle{});
			if (!texture) logger::warn("Menus Revamped: missing or unreadable skin art {}", path.filename().string());
			return texture;
		}

		// The preset's art for an image whose brush draws a listed stock texture: swapped on this widget only, and the
		// widget tint reset to white (alpha kept) so the neutral art shows as made. True when the image is now the skin's.
		// The colour an art swap is drawn with: its DarkStyle.h value for a white-shape PNG, else white (the art's own colours).
		void PaintSwap(UE::UObject* image, const uistyle::ArtSwap& swap, bool always)
		{
			float rgba[4];
			if (!ReadColor(image, rgba)) return;
			const auto want = !swap.tint ? std::array<float, 4>{ 1, 1, 1, rgba[3] } : darkstyle::Arr(darkstyle::Get().*swap.tint, rgba[3]);
			if (always || std::abs(rgba[0] - want[0]) > 0.001f || std::abs(rgba[1] - want[1]) > 0.001f || std::abs(rgba[2] - want[2]) > 0.001f)
				ui::Colour(image, want[0], want[1], want[2], want[3]);
		}

		// The preset's art for an image whose brush draws a listed stock texture: swapped on this widget only, and drawn in
		// the swap's colour (white-shape PNGs: their DarkStyle.h value; other art: white, alpha kept). True when the image is
		// now the skin's. A later pass re-applies the colour if the game reset it.
		bool SwapArt(UE::UObject* image, const uistyle::Skin& skin)
		{
			if (!skin.artSwapCount || !reflect::Usable(image)) return false;
			auto* current = ui::BrushResource(image);
			const auto resource = NameOf(current);
			for (int i = 0; i < skin.artSwapCount; ++i) {
				if (resource != skin.artSwaps[i].stock) continue;
				auto* art = SkinArt(skin.artSwaps[i].file);
				if (!art) return false;
				ui::Call set(image, L"SetBrushResourceObject");
				if (!set.Set("ResourceObject", art) || !set.RunGuarded()) return false;
				PaintSwap(image, skin.artSwaps[i], true);
				return true;
			}
			// already swapped (a later pass over the same widget): the brush draws one of our own textures
			for (int i = 0; i < skin.artSwapCount; ++i)
				if (auto* art = SkinArt(skin.artSwaps[i].file); art && art == current) {
					PaintSwap(image, skin.artSwaps[i], false);
					return true;
				}
			return false;
		}

		// Magic icons (stat icons, school glyphs) the game tints brown: the light colour instead. With the Dark pack the textures
		// are baked in rowIcon already, so the tint is white (no double colouring); rowIcon only without the pack.
		bool LightIcon(UE::UObject* image, std::string_view resource, const uistyle::Skin& skin)
		{
			if (!(resource.starts_with("T_MagicMenuIcon_") || resource.starts_with("T_MagicSchoolSimple_"))) return false;
			float rgba[4];
			if (!ReadColor(image, rgba)) return true;
			const auto icon = darkpack::Active() ? std::array<float, 4>{ 1, 1, 1, rgba[3] } : darkstyle::Arr(darkstyle::Get().rowIcon, rgba[3]);
			if (skin.recolor && (detailsskin::IsDarkInk(rgba) || detailsskin::IsWarmInk(rgba))) ui::Colour(image, icon[0], icon[1], icon[2], icon[3]);
			return true;
		}

		// One row's art, the same for every skinned list (Inventory, Magic): separators, equipped band and column lines take
		// the skin's swaps in their DarkStyle.h colours; icons the game tints brown take the row icon colour.
		void StyleRowArt(UE::UObject* row, const uistyle::Skin& skin)
		{
			static auto* imageClass = ui::Class(L"/Script/UMG.Image");
			if (!imageClass) return;
			for (auto* widget : ui::TreeWidgets(row, 80)) {
				if (!reflect::Usable(widget) || !widget->GetClass()->IsChildOf(imageClass)) continue;
				if (!SwapArt(widget, skin)) LightIcon(widget, NameOf(ui::BrushResource(widget)), skin);
			}
		}

		UE::UObject* RowColumn(UE::UObject* row, std::string_view name)
		{
			auto* horizontal = ui::FindInTree(row, "inv_entry_horizontal");
			ui::Call count(horizontal, L"GetChildrenCount");
			if (!count || !count.RunGuarded()) return nullptr;
			const int n = count.Get<int>("ReturnValue");
			if (n < 0 || n > 32) return nullptr;
			for (int i = 0; i < n; ++i) {
				ui::Call get(horizontal, L"GetChildAt");
				if (!get.Set("Index", i) || !get.RunGuarded()) return nullptr;
				auto* child = get.Get<UE::UObject*>("ReturnValue");
				if (child && NameOf(child) == name) return child;
			}
			return nullptr;
		}

		// Darkens one ScrollBarStyle's track and thumbs (RMI-068). A static material gets a dynamic instance written into
		// the style, and the brush's cached Slate handle is cleared so the new material draws even after the first paint
		// (the thumb stayed pale whenever it had painted before the swap). Returns the handles cleared.
		unsigned PaintScrollbarStyle(UE::UObject* owner, const char* property, unsigned& count)
		{
			const auto& skin = Current();
			static auto* style = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.ScrollBarStyle");
			static auto* brush = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.SlateBrush");
			static auto* dynamicType = ui::Class(L"/Script/Engine.MaterialInstanceDynamic");
			static auto* materialType = ui::Class(L"/Script/Engine.MaterialInterface");
			static auto* materialLibrary = ui::Class(L"/Script/Engine.KismetMaterialLibrary");
			if (!skin.recolor || !reflect::Usable(owner) || !style || !brush || !dynamicType || !materialType || !materialLibrary) return 0;
			const int styleOffset = reflect::Offset(owner->GetClass(), property);
			const int styleSize = ui::SizeOf(owner->GetClass(), property);
			const int resourceOffset = reflect::Offset(brush, "ResourceObject");
			const int brushSize = ui::SizeOf(style, "NormalThumbImage");
			if (styleOffset < 0 || styleSize <= 0 || resourceOffset < 0) return 0;
			unsigned resets = 0;
			const auto paint = [&](const char* field, bool track) {
				const int offset = reflect::Offset(style, field);
				if (offset < 0 || offset + resourceOffset + int(sizeof(UE::UObject*)) > styleSize) return;
				auto* brushAt = reinterpret_cast<std::uint8_t*>(owner) + styleOffset + offset;
				auto* resource = reflect::At<UE::UObject*>(owner, styleOffset + offset + resourceOffset);
				if (!resource || !reflect::Usable(*resource)) return;
				auto* material = *resource;
				if (!material->GetClass()->IsChildOf(dynamicType)) {
					if (!material->GetClass()->IsChildOf(materialType)) return;
					ui::Call create(materialLibrary->GetDefaultObject(false), L"CreateDynamicMaterialInstance");
					if (create.Size("ReturnValue") != sizeof(UE::UObject*) ||
						!create.Set("WorldContextObject", owner) || !create.Set("Parent", material) ||
						!create.RunGuarded()) return;
					auto* instance = create.Get<UE::UObject*>("ReturnValue");
					if (!reflect::Usable(instance) || !instance->GetClass()->IsChildOf(dynamicType)) return;
					material = instance;
				}
				const auto before = count;
				const auto setColor = [&](const wchar_t* parameter, float gray) {
					const std::array value{gray, gray, gray, 1.0f};
					ui::Call set(material, L"SetVectorParameterValue");
					if (set.Set("ParameterName", UE::FName(parameter, UE::EFindName::Add)) &&
						set.Set("Value", value) && set.RunGuarded()) ++count;
				};
				if (track) {
					setColor(L"DarkColor", skin.scrollTrack);
					setColor(L"LightColor", skin.scrollTrack);
				} else {
					setColor(L"BorderColor - Dark", skin.scrollBorder);
					setColor(L"BorderColor - Light", skin.scrollBorder);
					setColor(L"InsideColor - Dark", skin.scrollInside);
					setColor(L"InsideColor - Light", skin.scrollInside);
				}
				if (count > before && *resource != material) {
					*resource = material;
					if (ui::ResetBrushHandle(brushAt, brushSize)) ++resets;
				}
			};
			paint("VerticalBackgroundImage", true);
			for (const auto* field : {"NormalThumbImage", "HoveredThumbImage", "DraggedThumbImage"}) paint(field, false);
			return resets;
		}

		// RMI-073: Slate cuts text at its widget's box, and the title box kept the width of a shorter name (156, the width of
		// "Weapons"; log on 2cdd2c68). A minimum width that fits "Miscellaneous" (about 228) and a fresh layout pass keep
		// every tab name whole. The title may be a prefab text widget: its inner "text" then. Layout: every preset.
		void UnclipTitle(UE::UObject* title)
		{
			static auto* widgetClass = ui::Class(L"/Script/UMG.Widget");
			if (!widgetClass || !reflect::Usable(title)) return;
			static const int clippingOffset = reflect::Offset(widgetClass, "Clipping");
			if (clippingOffset >= 0)
				if (const auto* clip = reflect::At<std::uint8_t>(title, clippingOffset); clip && *clip) {
					ui::Call set(title, L"SetClipping");
					if (set.Set("InClipping", std::uint8_t(0))) set.Run();
				}
			// RMI-073: a different slice of "Miscellaneous" shows each time (Miscellan, liscellane, cellaneou): the text
			// scrolls inside a box narrower than the name. The title is a WBP_AltarTextBlock (a user widget, log on
			// a22c29b8) whose text is the inner "text_block"; no scrolling plus a wide enough box keeps it whole.
			constexpr float kTitleWidth = 240.0f;
			// The prefab keeps its own "Min Desired Width" and "IsScrollingEnable" and re-applies them to its text on every
			// SetText (owner screenshot on 7c938a78: still cut at the old width). Set them on this instance first.
			if (const int at = reflect::Offset(title->GetClass(), "Min Desired Width"); at >= 0 && ui::SizeOf(title->GetClass(), "Min Desired Width") == 4)
				*reflect::At<float>(title, at) = kTitleWidth;
			if (const int at = reflect::Offset(title->GetClass(), "IsScrollingEnable"); at >= 0 && ui::SizeOf(title->GetClass(), "IsScrollingEnable") == 1)
				*reflect::At<bool>(title, at) = false;
			UE::UObject* label = title;
			if (!ui::Call(label, L"SetMinDesiredWidth")) label = ui::FindInTree(title, "text_block");
			if (!label) label = ui::FindInTree(title, "text");
			if (!label) return;
			const bool off = false;
			ui::CallFirst(label, L"SetScrollingEnabled", &off, sizeof(off));
			const bool widened = bool(ui::Call(label, L"SetMinDesiredWidth"));
			if (widened) ui::Float(label, L"SetMinDesiredWidth", kTitleWidth);
			// The title sits in a RetainerBox (inv_cont_AnimationRetainerforText) that draws it through an effect material for
			// its reveal animation; that mask cut the text at a fixed width whatever the text's own width (owner screenshots on
			// 7c938a78 and 76e583e0). Drawn directly, the whole name shows.
			static auto* retainerClass = ui::Class(L"/Script/UMG.RetainerBox");
			UE::UObject* parent = title;
			for (int depth = 0; depth < 4 && parent && reflect::Usable(parent); ++depth) {
				if (retainerClass && parent->GetClass()->IsChildOf(retainerClass)) {
					// only while it is on: each switch makes the engine drop and rebuild the retainer's render target (RMI-084)
					static const int retainOffset = reflect::Offset(retainerClass, "bRetainRender");
					const auto* retaining = retainOffset >= 0 ? reflect::At<bool>(parent, retainOffset) : nullptr;
					if (!retaining || *retaining) {
						const bool retain = false;
						ui::CallFirst(parent, L"SetRetainRendering", &retain, sizeof(retain));
					}
				}
				ui::Call up(parent, L"GetParent");
				parent = up && up.Run() ? up.Get<UE::UObject*>("ReturnValue") : nullptr;
			}
			static int prepasses = 0;
			if (prepasses < 3) {
				++prepasses;
			}
			for (auto* widget : { label, title })
				if (ui::Call prepass(widget, L"ForceLayoutPrepass"); widget && prepass) prepass.Run();
		}

		// ---- item details ----
		reflect::Handle g_details;
		std::vector<reflect::Handle> g_unhoveredRules;
		const Target* g_detailsTarget = &kInventory;

		// the action button (Equip, Use...): its fill and arrows gray, its label light; alpha stays the game's (hover fades)
		void StyleDetailsButton(UE::UObject* button)
		{
			const auto& skin = Current();
			static auto* imageClass = ui::Class(L"/Script/UMG.Image");
			static auto* textClass = ui::Class(L"/Script/UMG.TextBlock");
			if (!skin.recolor || !imageClass || !textClass) return;
			for (auto* widget : ui::TreeWidgets(button, 3000)) {
				if (!reflect::Usable(widget)) continue;
				float rgba[4];
				if (!ReadColor(widget, rgba)) continue;
				if (widget->GetClass()->IsChildOf(imageClass)) {
					if (SwapArt(widget, skin)) continue;   // arrows and rules
					// the rules' middle stretch: colour-only, the stock brown in its brush (census on a593c0f7)
					float brush[4];
					if (skin.buttonRule[3] > 0 && !ui::BrushResource(widget) && ReadBrushTint(widget, brush) && detailsskin::IsButtonRule(brush, skin.buttonRule.data())) {
						if (brush[3] > 0.60f && brush[3] < 0.65f &&
							std::none_of(g_unhoveredRules.begin(), g_unhoveredRules.end(),
								[&](const auto& rule) { return reflect::Get(rule) == widget; }))
							g_unhoveredRules.push_back(reflect::Hold(widget));
						const float line[4]{ skin.buttonRule[0], skin.buttonRule[1], skin.buttonRule[2], brush[3] };
						WriteBrushTint(widget, line);
						ui::Colour(widget, 1, 1, 1, rgba[3]);
						continue;
					}
					const auto fill = WithAlpha(skin.buttonFill, rgba[3]);
					ui::Colour(widget, fill[0], fill[1], fill[2], fill[3]);
				} else if (widget->GetClass()->IsChildOf(textClass)) {
					const auto ink = WithAlpha(skin.ink, rgba[3]);
					uix::Ink(widget, ink.data());
				}
			}
		}

		void OnDetailsButtonEvent(UE::UObject* button, UE::UFunction*, void*)
		{
			auto* details = reflect::Get(g_details);
			if (!details || !reflect::Usable(button) || !ui::IsWithin(button, details)) return;
			StyleDetailsButton(button);
		}

		// A list entry inside the details view (an effect line): the game rebuilds it for each item after the details pass,
		// with the stock art and ink (owner screenshot on a22c29b8: the first effect's stat icons brown). Restyled when rebuilt.
		void StyleDetailsEntry(UE::UObject* entry)
		{
			const auto& skin = Current();
			static auto* imageClass = ui::Class(L"/Script/UMG.Image");
			static auto* textClass = ui::Class(L"/Script/UMG.TextBlock");
			if (!skin.recolor || !imageClass || !textClass) return;
			for (auto* widget : ui::TreeWidgets(entry, 120)) {
				if (!reflect::Usable(widget)) continue;
				if (widget->GetClass()->IsChildOf(imageClass)) {
					if (SwapArt(widget, skin)) continue;
					// the lines draw the white stat icons (T_Property_1_*_White) tinted brown themselves: light ink instead
					if (NameOf(ui::BrushResource(widget)).starts_with("T_Property_1_")) {
						float rgba[4];
						if (ReadColor(widget, rgba)) ui::Colour(widget, skin.ink[0], skin.ink[1], skin.ink[2], rgba[3]);
					}
				}
				else if (widget->GetClass()->IsChildOf(textClass)) {
					float rgba[4];
					if (!ReadColor(widget, rgba) || !detailsskin::IsDarkInk(rgba)) continue;
					const auto ink = WithAlpha(skin.ink, rgba[3]);
					uix::Ink(widget, ink.data());
				}
			}
		}

		// Effect lines: WBP_LegacyMenu_AddedEffects entries the legacy effects list creates into its VNavigableScrollBox, outside
		// the widget tree (no census line for them on 0d025571). The class is shared with other menus, so only while the
		// Inventory details view is on screen.
		void OnEffectLineEvent(UE::UObject* line, UE::UFunction* function, void*);

		std::set<UE::UClass*> g_listEntryClasses;   // entry classes generated by lists inside the details view

		void OnDetailsEntryEvent(UE::UObject* entry, UE::UFunction* function, void*)
		{
			const auto name = pe::FunctionName(function);
			if (name != "OnListItemObjectSet" && name != "Construct" && !name.starts_with("Set") && !name.starts_with("Update")) return;
			if (!reflect::Usable(entry)) return;
			auto* details = reflect::Get(g_details);
			if (g_listEntryClasses.contains(entry->GetClass()) || (details && ui::IsWithin(entry, details))) StyleDetailsEntry(entry);
		}

		void OnEffectLineEvent(UE::UObject* line, UE::UFunction* function, void*)
		{
			const auto name = pe::FunctionName(function);
			if (name != "OnListItemObjectSet" && name != "Construct" && !name.starts_with("Set") && !name.starts_with("Update")) return;
			auto* details = reflect::Get(g_details);
			if (details && reflect::Usable(line) && ui::ShownOnScreen(details)) StyleDetailsEntry(line);
		}

		// The effect lines are entries a list view generates (owner screenshot on 76e583e0: every line's stat icons brown).
		// Generated entries are not in the details widget tree, so they are asked from the list and re-styled when rebound.
		void StyleListEntries(UE::UObject* list)
		{
			ui::Call entries(list, L"GetDisplayedEntryWidgets");
			if (!entries || entries.Size("ReturnValue") != 16 || !entries.RunGuarded()) return;
			struct Array { UE::UObject** data; std::int32_t count, capacity; };
			const auto array = entries.Get<Array>("ReturnValue");
			for (int i = 0; array.data && i < std::min<int>(array.count, 64); ++i) {
				auto* entry = array.data[i];
				if (!reflect::Usable(entry)) continue;
				if (g_listEntryClasses.insert(entry->GetClass()).second) pe::WatchPost(entry->GetClass(), &OnDetailsEntryEvent);
				StyleDetailsEntry(entry);
			}
			if (array.data) UE::FMemory::Free(array.data);
		}

		bool Readable(const void* a_ptr, std::size_t a_bytes)
		{
			MEMORY_BASIC_INFORMATION info{};
			if (!a_ptr || !::VirtualQuery(a_ptr, &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
			constexpr DWORD kRead = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
			                        PAGE_EXECUTE_WRITECOPY;
			if (!(info.Protect & kRead) || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
			return static_cast<const std::uint8_t*>(a_ptr) + a_bytes <=
			       static_cast<const std::uint8_t*>(info.BaseAddress) + info.RegionSize;
		}

		// The perk descriptions' highlighted words are brown rows of a rich-text style table shared with the Stats menu
		// (DT_StatsMenu_SkillDescriptionRichTextStyle_PerLevelDescription, rows 0.12 0.07 0.037). For this widget only:
		// the warm rows take the skin's emphasis, the widget rebuilds its style from them (SetTextStyleSet null, then the
		// table), and the rows are restored in the same call, so the Stats menu keeps the table's brown.
		// allRows: every warm row takes this colour (the Magic Spell Effectiveness line) instead of the perk/emphasis split
		// keepRows: leave the new colours in the table (a table only this menu uses): restoring them right after the rebuild
		// left the Spell Effectiveness text dark (4bb22392), the text reads the table later.
		bool RebuildEmphasis(UE::UObject* rich, const uistyle::Skin& skin, const uistyle::Color* allRows = nullptr, bool keepRows = false,
			bool logRows = false)
		{
			static auto* tableClass = ui::Class(L"/Script/Engine.DataTable");
			static auto* textStyle = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.TextBlockStyle");
			if ((!allRows && skin.emphasisInk[3] <= 0) || !tableClass || !textStyle) return false;
			const int setOffset = reflect::Offset(rich->GetClass(), "TextStyleSet");
			auto** setAt = setOffset >= 0 ? reflect::At<UE::UObject*>(rich, setOffset) : nullptr;
			auto* table = setAt ? *setAt : nullptr;
			if (!reflect::Usable(table) || !table->GetClass()->IsChildOf(tableClass)) return false;
			static const int rowStructOffset = reflect::Offset(tableClass, "RowStruct");
			auto** rowStructAt = rowStructOffset >= 0 ? reflect::At<UE::UStruct*>(table, rowStructOffset) : nullptr;
			auto* rowStruct = rowStructAt ? *rowStructAt : nullptr;
			if (!rowStruct || !reflect::Usable(rowStruct)) return false;
			const int styleOffset = reflect::Offset(rowStruct, "TextStyle");
			static const int colorOffset = reflect::Offset(textStyle, "ColorAndOpacity");
			if (styleOffset < 0 || colorOffset < 0) return false;
			std::vector<std::pair<float*, std::array<float, 4>>> saved;
			const auto* map = reinterpret_cast<const std::uint8_t*>(table) + rowStructOffset + sizeof(void*);
			datatablerows::VisitRows(map, Readable, [&](std::uint64_t key, std::uint8_t* row) {
				auto* color = reinterpret_cast<float*>(row + styleOffset + colorOffset);   // FSlateColor.SpecifiedColor
				if (!Readable(color, 16) || (!allRows && !detailsskin::IsWarmInk(color))) return;
				UE::FName name;
				static_assert(sizeof(name) == sizeof(key));
				std::memcpy(&name, &key, sizeof(key));
				// bold rows are the highlighted words (emphasis colour); the other brown rows are normal text (perk ink)
				const auto rowName = pe::Utf8(name.ToString());
				const bool bold = detailsskin::IsEmphasisRow(rowName);
				if (logRows)   // TEMP-DIAG: the Spell Effectiveness rows (it stayed dark on 4c0fd98f); record in the KB
					logger::warn("TEMP-DIAG MRMAGICDARK rich row {} c={:.3f},{:.3f},{:.3f},{:.2f}", rowName, color[0], color[1], color[2], color[3]);
				saved.push_back({ color, { color[0], color[1], color[2], color[3] } });
				std::copy_n((allRows ? *allRows : bold ? skin.emphasisInk : skin.perkInk).data(), 3, color);
			});
			if (saved.empty()) return false;
			UE::UObject* none = nullptr;
			ui::CallFirst(rich, L"SetTextStyleSet", &none, sizeof(none));
			ui::CallFirst(rich, L"SetTextStyleSet", &table, sizeof(table));
			if (!keepRows)
				for (auto& [color, original] : saved) std::copy_n(original.data(), 4, color);
			return true;
		}

		// One perk level line of the skill sub-tab (WBP_ModernMenu_Stats_LevelDescription). Every line draws the same
		// brown (0.072 0.038 0.028, log on 2cdd2c68); the game dims locked lines through their render opacity, so all of
		// them take the perk ink. Its own EnableTextColor/DisableTextColor (this instance only) feed its UpdateState.
		unsigned StyleLevelDescription(UE::UObject* entry)
		{
			const auto& skin = Current();
			static auto* textClass = ui::Class(L"/Script/UMG.TextBlock");
			static auto* richClass = ui::Class(L"/Script/UMG.RichTextBlock");
			static auto* textStyle = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.TextBlockStyle");
			if (!skin.recolor) return 0;
			const auto setSlate = [&](const char* name, const uistyle::Color& c) {
				const int offset = reflect::Offset(entry->GetClass(), name);
				if (offset < 0 || ui::SizeOf(entry->GetClass(), name) < 20) return;
				if (auto* at = reflect::At<float>(entry, offset)) std::copy_n(c.data(), 3, at);   // FSlateColor.SpecifiedColor
			};
			setSlate("EnableTextColor", skin.perkInk);
			setSlate("DisableTextColor", skin.perkInk);
			unsigned inked = 0;
			for (auto* widget : ui::TreeWidgets(entry, 80)) {
				if (!reflect::Usable(widget)) continue;
				auto* type = widget->GetClass();
				if (textClass && type->IsChildOf(textClass)) {
					float rgba[4];
					if (!ReadColor(widget, rgba) || !(detailsskin::IsWarmInk(rgba) || detailsskin::IsDarkInk(rgba))) continue;
					const auto ink = WithAlpha(skin.perkInk, rgba[3]);
					uix::Ink(widget, ink.data()); ++inked;
				} else if (richClass && textStyle && type->IsChildOf(richClass)) {
					const int overrideOffset = reflect::Offset(type, "bOverrideDefaultStyle");
					const int styleOffset = reflect::Offset(type, "DefaultTextStyleOverride");
					const int colorOffset = reflect::Offset(textStyle, "ColorAndOpacity");
					if (overrideOffset < 0 || styleOffset < 0 || colorOffset < 0) continue;
					const auto* overriding = reflect::At<bool>(widget, overrideOffset);
					const auto* rgba = reflect::At<float>(widget, styleOffset + colorOffset);
					if (!overriding || !rgba) continue;
					RebuildEmphasis(widget, skin);
					if (*overriding && !(detailsskin::IsWarmInk(rgba) || detailsskin::IsDarkInk(rgba))) continue;
					ui::Call set(widget, L"SetDefaultColorAndOpacity");
					auto* color = static_cast<std::uint8_t*>(set.At("InColorAndOpacity"));
					if (!color || set.Size("InColorAndOpacity") < 20) continue;
					const float value[4]{ skin.perkInk[0], skin.perkInk[1], skin.perkInk[2], *overriding ? rgba[3] : 1.0f };
					std::memcpy(color, value, sizeof(value));
					color[16] = 0;   // ESlateColorStylingMode::UseColor_Specified
					if (set.RunGuarded()) ++inked;
				}
			}
			return inked;
		}

		// A rich text line of the Magic page (Spell Effectiveness): its warm table rows and its default colour take the light ink.
		bool InkRich(UE::UObject* rich, const uistyle::Skin& skin)
		{
			static auto* textStyle = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.TextBlockStyle");
			constexpr bool keepRows = true;   // DT_UI_Magic_SpellEffectiveness_RichTextStyle: WBP_ModernMenu_MagicMenu only
			static bool s_logged = false;   // TEMP-DIAG
			bool inked = RebuildEmphasis(rich, skin, &skin.ink, keepRows, !s_logged);
			if (!s_logged) {
				s_logged = true;
				const int over = reflect::Offset(rich->GetClass(), "bOverrideDefaultStyle");
				const auto* overriding = over >= 0 ? reflect::At<bool>(rich, over) : nullptr;
				logger::warn("TEMP-DIAG MRMAGICDARK rich text='{}' override={} rebuilt={}", uix::GetText(rich), overriding ? int(*overriding) : -1, inked);
			}
			const int styleOffset = reflect::Offset(rich->GetClass(), "DefaultTextStyleOverride");
			const int colorOffset = textStyle ? reflect::Offset(textStyle, "ColorAndOpacity") : -1;
			const auto* rgba = styleOffset >= 0 && colorOffset >= 0 ? reflect::At<float>(rich, styleOffset + colorOffset) : nullptr;
			if (rgba) {   // only called for the Spell Effectiveness line: its colours fail the warm test (log on 243ea76d)
				ui::Call set(rich, L"SetDefaultColorAndOpacity");
				auto* color = static_cast<std::uint8_t*>(set.At("InColorAndOpacity"));
				if (color && set.Size("InColorAndOpacity") >= 20) {
					const float value[4]{ skin.ink[0], skin.ink[1], skin.ink[2], 1.0f };
					std::memcpy(color, value, sizeof(value));
					color[16] = 0;   // ESlateColorStylingMode::UseColor_Specified
					inked = set.RunGuarded() || inked;
				}
			}
			return inked;
		}

		void StyleDetails(UE::UObject* details)
		{
			const auto& skin = Current();
			static auto* imageClass = ui::Class(L"/Script/UMG.Image");
			static auto* textClass = ui::Class(L"/Script/UMG.TextBlock");
			static auto* instanceClass = ui::Class(L"/Script/Engine.MaterialInstance");
			static auto* scrollBoxClass = ui::Class(L"/Script/UMG.ScrollBox");
			static auto* listViewClass = ui::Class(L"/Script/UMG.ListViewBase");
			if (!skin.recolor || !imageClass || !textClass || !reflect::Usable(details)) return;
			static const int colorOffset = reflect::Offset(textClass, "ColorAndOpacity");
			static const int parentOffset = instanceClass ? reflect::Offset(instanceClass, "Parent") : -1;
			unsigned barCalls = 0;
			const auto tint = [&](UE::UObject* widget, const float* c) { ui::Colour(widget, c[0], c[1], c[2], c[3]); };
			UE::UObject* sigil = nullptr;
			for (auto* widget : ui::TreeWidgets(details, 3000)) {
				if (!reflect::Usable(widget)) continue;
				auto* type = widget->GetClass();
				const auto typeName = NameOf(type);
				if (typeName == "WBP_OriginalMenu_InventoryItemSpecificContentSigilStone_C") sigil = widget;
				if (typeName.find("Button") != std::string::npos && typeName.starts_with("WBP_ModernPrefab")) {
					pe::WatchPost(type, &OnDetailsButtonEvent);
					StyleDetailsButton(widget);
					continue;
				}
				if (typeName.find("Entry") != std::string::npos && typeName.ends_with("_C")) {   // effect lines and other entries
					pe::WatchPost(type, &OnDetailsEntryEvent);
					StyleDetailsEntry(widget);
					continue;
				}
				if (typeName.starts_with("WBP_ModernMenu_Stats_LevelDescription")) {
					StyleLevelDescription(widget);
					continue;
				}
				if (listViewClass && type->IsChildOf(listViewClass)) {   // effect lines and other generated entries
					StyleListEntries(widget);
					continue;
				}
				if (scrollBoxClass && type->IsChildOf(scrollBoxClass)) {   // the skill sub-tab's perk list
					PaintScrollbarStyle(widget, "WidgetBarStyle", barCalls);
					continue;
				}
				if (type->IsChildOf(imageClass)) {
					if (g_detailsTarget == &kMagic) {
						static std::set<std::string> s_brushes;   // TEMP-DIAG: what each Magic details image draws (record in the KB)
						const auto brush = NameOf(widget) + " " + NameOf(ui::BrushResource(widget));
						if (s_brushes.size() < 40 && s_brushes.insert(brush).second) logger::warn("TEMP-DIAG MRMAGICDARK details image {}", brush);
					}
					if (SwapArt(widget, skin)) continue;
					auto* resource = ui::BrushResource(widget);
					if (!resource || !reflect::Usable(resource)) continue;
					std::string parent;
					if (parentOffset >= 0 && resource->GetClass()->IsChildOf(instanceClass))
						if (auto** at = reflect::At<UE::UObject*>(resource, parentOffset); at && *at && reflect::Usable(*at))
							parent = NameOf(*at);
					const auto resourceName = NameOf(resource);
					if (LightIcon(widget, resourceName, skin)) continue;
					if (detailsskin::IsDescriptionRule(resourceName) ||
						(resourceName == "T_Line_02" && sigil && ui::IsWithin(widget, sigil))) {
						if (auto* art = SkinArt(L"RowSeparator.png")) {
							ui::Call set(widget, L"SetBrushResourceObject");
							if (set.Set("ResourceObject", art) && set.RunGuarded()) {
								float rgba[4];
								if (ReadColor(widget, rgba)) tint(widget, WithAlpha(skin.ornament, rgba[3]).data());
								if (float brush[4]; ReadBrushTint(widget, brush)) {
									detailsskin::NeutralBrush(brush, 1.0f);
									WriteBrushTint(widget, brush);
								}
							}
						}
						continue;
					}
					switch (detailsskin::Classify(resourceName, parent)) {
					case detailsskin::Role::Page: {
						// keeps M_MaskedPage (its shape leaves the 3D preview open); the preset's gray hides the parchment
						float rgba[4];
						if (!ReadColor(widget, rgba)) break;
						// the Dark pack bakes the parchment to this colour already: white then (no double darkening)
						detailsskin::PageTint(rgba, darkpack::Active() ? 1.0f : skin.pageGray);
						tint(widget, rgba);
						break;
					}
					case detailsskin::Role::Ornament: {
						if (float brush[4]; ReadBrushTint(widget, brush)) {   // the art is white; its brush carries a gold 0.25/0.18/0.11
							detailsskin::NeutralBrush(brush, 1.0f);
							WriteBrushTint(widget, brush);
						}
						tint(widget, skin.ornament.data());
						break;
					}
					case detailsskin::Role::Pattern:
						if (darkpack::BakedLaurelTint()) {
							static constexpr float neutral[]{ 1, 1, 1, 1 };
							tint(widget, neutral);
						} else {
							tint(widget, skin.pattern.data());   // Phase 4 packs and stock fallback
						}
						break;
					case detailsskin::Role::Band: tint(widget, skin.band.data()); break;
					case detailsskin::Role::Ring: {   // the level ring: a dynamic instance of M_UI_CircularProgess
						ui::Call set(resource, L"SetVectorParameterValue");
						if (set.Set("ParameterName", UE::FName(L"Color", UE::EFindName::Add)) && set.Set("Value", skin.ring))
							set.RunGuarded();
						break;
					}
					default: break;
					}
				} else if (colorOffset >= 0 && type->IsChildOf(textClass)) {
					const auto* rgba = reflect::At<float>(widget, colorOffset);
					if (!rgba || (!detailsskin::IsDarkInk(rgba) && !detailsskin::IsDescriptionHeading(rgba))) continue;
					const auto ink = WithAlpha(skin.ink, rgba[3]);
					uix::Ink(widget, ink.data());
				}
			}
		}

		void OnDetailsUpdated(UE::UObject* details, UE::UFunction* function, void*)
		{
			// the Magic path only for the Magic details (after a Magic visit the target stayed Magic for the Inventory's details)
			if (g_detailsTarget == &kMagic && NameOf(details->GetClass()) == kMagic.detailsClass) {
				// Magic details events are not known yet: restyle after any of its own events except per-frame ones, once a frame.
				const auto event = pe::FunctionName(function);
				if (event == "Tick" || event.find("Anim") != std::string_view::npos) return;
				static std::set<std::string> s_seen;   // TEMP-DIAG: the Magic details event names (record in the KB)
				if (s_seen.size() < 40 && s_seen.insert(std::string(event)).second)
					logger::warn("TEMP-DIAG MRMAGICDARK details event {}", event);
				static double s_last = -1;
				const double now = ui::RealTimeSeconds();
				if (now >= 0 && now == s_last) return;
				s_last = now;
				StyleDetails(details);
				return;
			}
			static constexpr std::string_view kEvents[]{ "Construct", "ToggleDetails", "ChangeVisibilityItemDetailsPanel",
				"UpdateHoveredItemProperties", "ChangeSpecificDetailsForHoveredItem", "ChangeSpecificDetailsWithFormID",
				"ChangeSpecificDetailsWithFormType", "SwitchSubTabs", "SwitchPreview", "UpdateDescription" };
			const auto name = pe::FunctionName(function);
			if (std::find(std::begin(kEvents), std::end(kEvents), name) != std::end(kEvents)) StyleDetails(details);
		}

		// ---- footers ----
		std::array<reflect::Handle, 4> g_footers;

		void InkFooter(UE::UObject* footer)
		{
			const auto& skin = Current();
			if (!skin.recolor) return;
			auto* label = ui::FindInTree(footer, "text");
			if (!label) label = ui::FindByClassPrefix(footer, "WBP_AltarTextBlock");
			if (label) uix::Ink(label, skin.ink.data());
		}

		void OnFooterUpdated(UE::UObject* footer, UE::UFunction* function, void*)
		{
			const auto name = pe::FunctionName(function);
			if (name != "UpdateText" && name != "UpdateTextApperance") return;
			if (std::any_of(g_footers.begin(), g_footers.end(), [footer](const auto& handle) { return reflect::Get(handle) == footer; }))
				InkFooter(footer);
		}
	}

	namespace
	{
		reflect::Handle g_title;

		reflect::Handle g_titleLabel;

		// RMI-073: inside the prefab's own SetText (same frame): our label takes the new name and the native text stays hidden.
		// The header layout runs on its own timer, so waiting for it showed the old name or both texts (owner on a593c0f7).
		void KeepTitleLabel(UE::UObject* title, UE::UFunction* function, void* params)
		{
			auto* label = reflect::Get(g_titleLabel);
			auto* text = label ? ui::FindInTree(title, "text_block") : nullptr;
			if (!text) return;
			if (params) {
				std::optional<std::string> requested;
				auto* signature = reinterpret_cast<UE::UStruct*>(function);
				const auto fields = reflect::Fields(signature);
				if (!fields.empty()) {
					const auto& [name, offset] = fields.front();
					if (offset >= 0 && ui::SizeOf(signature, name) == sizeof(UE::FText) &&
						offset + static_cast<int>(sizeof(UE::FText)) <= signature->propertiesSize) {
						const auto* value = reinterpret_cast<const UE::FText*>(static_cast<const std::uint8_t*>(params) + offset);
						requested = reflect::Text(*value);
					}
				}
				const auto native = uix::GetText(text);
				const auto want = inventoryheaders::TitleText(requested ? std::optional<std::string_view>(*requested) : std::nullopt, native);
				if (uix::GetText(label) != want) uix::SetText(label, std::string(want));
			}

			if (ui::RenderOpacity(title) > 0) ui::Float(title, L"SetRenderOpacity", 0.0f);
		}

		// the prefab re-applies its variables on its own updates (SetText when the tab changes): keep the title whole after
		void OnTitleEvent(UE::UObject* widget, UE::UFunction* function, void* params)
		{
			if (widget != reflect::Get(g_title)) return;
			const auto name = pe::FunctionName(function);

			if (name == "SetText" || name == "PreConstruct" || name == "Construct" || name == "ToggleScrollingText") {
				UnclipTitle(widget);
				KeepTitleLabel(widget, function, name == "SetText" ? params : nullptr);
			}
		}
	}

	void StyleTitle(UE::UObject* title)
	{
		if (!reflect::Usable(title)) return;
		g_title = reflect::Hold(title);
		static UE::UClass* watched = nullptr;
		if (watched != title->GetClass() && pe::WatchPost(title->GetClass(), &OnTitleEvent)) watched = title->GetClass();
		const auto& skin = Current();
		if (skin.recolor) uix::Ink(title, skin.ink.data());
		UnclipTitle(title);
	}


	void SetTitleLabel(UE::UObject* label) { g_titleLabel = label ? reflect::Hold(label) : reflect::Handle{}; }

	void RefreshInventoryTitle()
	{
		auto* label = reflect::Get(g_titleLabel);
		auto* title = reflect::Get(g_title);
		auto* text = label && title ? ui::FindInTree(title, "text_block") : nullptr;
		if (!text) return;
		const auto current = uix::GetText(text);
		if (!current.empty() && uix::GetText(label) != current) uix::SetText(label, current);
		if (ui::RenderOpacity(title) > 0) ui::Float(title, L"SetRenderOpacity", 0.0f);
	}

	void RefreshButtonRules()
	{
		const auto& skin = Current();
		if (skin.buttonRule[3] <= 0 || !reflect::Get(g_details)) return;
		std::erase_if(g_unhoveredRules, [](const auto& rule) { return !reflect::Get(rule); });
		for (const auto& rule : g_unhoveredRules) {
			auto* widget = reflect::Get(rule);
			float brush[4];
			if (!ReadBrushTint(widget, brush) || !detailsskin::IsBrushRule(brush)) continue;
			const float line[4]{ skin.buttonRule[0], skin.buttonRule[1], skin.buttonRule[2], brush[3] };
			WriteBrushTint(widget, line);
		}
	}

	void InkText(UE::UObject* label)
	{
		const auto& skin = Current();
		if (skin.recolor && reflect::Usable(label)) uix::Ink(label, skin.ink.data());
	}

	void StyleRow(UE::UObject* row, const Target& target)
	{
		const auto& skin = Current();
		if (!reflect::Usable(row)) return;
		if (!skin.recolor) return;
		if (auto* name = RowColumn(row, target.rowName)) uix::Ink(name, skin.ink.data());
		for (int i = 0; i < target.rowValueCount; ++i)
			if (auto* label = ui::FindInTree(RowColumn(row, target.rowValues[i]), "inv_entry_value")) uix::Ink(label, skin.ink.data());
		// the row's art (separator T_List_Default, equipped band T_List_equip_V2, column lines T_Line_V) swapped for the
		// preset's on this row only, shared with the Magic rows (Slate tints cannot brighten or neutralise baked brown)
		StyleRowArt(row, skin);
		// the equipped pen scribble (M_UI_Scribble) on dark rows
		if (skin.hideEquippedScribble)
			if (auto* scribble = ui::FindInTree(row, "inv_entry_scribble")) ui::Colour(scribble, 1.0f, 1.0f, 1.0f, 0.0f);
	}

	void StyleMagicRow(UE::UObject* row)
	{
		const auto& skin = Current();
		static auto* textClass = ui::Class(L"/Script/UMG.TextBlock");
		if (!skin.recolor || !textClass || !reflect::Usable(row)) return;
		// text: the same row ink as the Inventory (DarkStyle.h rowText); colour-coded text keeps its colour
		for (auto* widget : ui::TreeWidgets(row, 80)) {
			float rgba[4];
			if (!reflect::Usable(widget) || !widget->GetClass()->IsChildOf(textClass) || !ReadColor(widget, rgba)) continue;
			if (detailsskin::IsDarkInk(rgba) || detailsskin::IsWarmInk(rgba)) uix::Ink(widget, WithAlpha(skin.ink, rgba[3]).data());
		}
		StyleRowArt(row, skin);
	}

	void InkTree(UE::UObject* root, int a_limit)
	{
		const auto& skin = Current();
		static auto* textClass = ui::Class(L"/Script/UMG.TextBlock");
		if (!skin.recolor || !textClass || !reflect::Usable(root)) return;
		for (auto* widget : ui::TreeWidgets(root, a_limit)) {
			float rgba[4];
			if (!reflect::Usable(widget) || !widget->GetClass()->IsChildOf(textClass) || !ReadColor(widget, rgba)) continue;
			if (detailsskin::IsDarkInk(rgba) || detailsskin::IsWarmInk(rgba)) uix::Ink(widget, WithAlpha(skin.ink, rgba[3]).data());
		}
	}

	namespace
	{
		reflect::Handle g_magicMenu;
		std::vector<reflect::Handle> g_magicPages;

	}

	void StyleMagicPage(UE::UObject* menu, bool rescan)
	{
		const auto& skin = Current();
		static auto* imageClass = ui::Class(L"/Script/UMG.Image");
		static auto* richClass = ui::Class(L"/Script/UMG.RichTextBlock");
		static auto* textClass = ui::Class(L"/Script/UMG.TextBlock");
		if (!skin.recolor || !imageClass || !reflect::Usable(menu)) return;
		// a page widget gone (the details view re-created): look again, at most once a second
		static ULONGLONG s_nextLost = 0;
		if (!rescan && reflect::Get(g_magicMenu) == menu && GetTickCount64() >= s_nextLost &&
			std::any_of(g_magicPages.begin(), g_magicPages.end(), [](const auto& page) { return !reflect::Get(page); })) {
			s_nextLost = GetTickCount64() + 1000;
			rescan = true;
		}
		if (rescan || reflect::Get(g_magicMenu) != menu) {
			g_magicMenu = reflect::Hold(menu);
			g_magicPages.clear();
			auto* pageArt = SkinArt(L"MagicPage.png");
			// the Spell Effectiveness texts by widget name (any game language; static scan of WBP_ModernMenu_MagicMenu): the line
			// under the bars (SpellEffectiveness_Box, the game's spelling) and the explanation's title
			UE::UObject* effectiveness[]{ ui::FindInTree(menu, "Magic_SpellEffectivenes_Value"),
				ui::FindInTree(menu, "Magic_Active_SpellEffectiveness_Title") };
			for (auto* widget : ui::TreeWidgets(menu, 3000)) {
				if (!reflect::Usable(widget)) continue;
				if (richClass && widget->GetClass()->IsChildOf(richClass)) {
					// the Spell Effectiveness line: DT_UI_Magic_SpellEffectiveness_RichTextStyle (used by the Magic menu only)
					const int setOffset = reflect::Offset(widget->GetClass(), "TextStyleSet");
					auto** table = setOffset >= 0 ? reflect::At<UE::UObject*>(widget, setOffset) : nullptr;
					if (table && reflect::Usable(*table) && NameOf(*table).find("SpellEffectiveness") != std::string::npos)
						InkRich(widget, skin);
					continue;
				}
				if (textClass && widget->GetClass()->IsChildOf(textClass)) {
					// the visible "95% Spell Effectiveness" line (the rich text above is its tooltip, already light): row ink
					if (std::any_of(std::begin(effectiveness), std::end(effectiveness),
							[&](UE::UObject* holder) { return holder && (holder == widget || ui::IsWithin(widget, holder)); })) {
						float rgba[4];
						if (ReadColor(widget, rgba)) uix::Ink(widget, WithAlpha(skin.ink, rgba[3]).data());
					}
					continue;
				}
				if (!widget->GetClass()->IsChildOf(imageClass)) continue;
				auto* brush = ui::BrushResource(widget);
				if (uistyle::IsMagicPage(NameOf(brush)) || (pageArt && brush == pageArt)) g_magicPages.push_back(reflect::Hold(widget));
			}
		}
		for (const auto& page : g_magicPages)
			if (auto* widget = reflect::Get(page)) SwapArt(widget, skin);   // MagicPage.png in DarkStyle.h panel
	}

	void WatchFooters(UE::UObject* mainPart, UE::UObject* menu, const Target& target)
	{
		for (int i = 0; i < target.footerCount && i < int(g_footers.size()); ++i) {
			auto* footer = ui::FindInTree(mainPart, target.footers[i]);
			if (!footer && mainPart != menu) footer = ui::FindInTree(menu, target.footers[i]);
			g_footers[i] = reflect::Hold(footer);
			if (!footer) continue;
			pe::WatchPost(footer->GetClass(), &OnFooterUpdated);
			InkFooter(footer);
		}
	}

	void StyleListScrollbar(UE::UObject* list)
	{
		const auto& skin = Current();
		if (!skin.recolor) return;
		ui::Call get(list, L"GetListView");
		auto* inner = get.Size("ReturnValue") == sizeof(UE::UObject*) && get.RunGuarded()
			? get.Get<UE::UObject*>("ReturnValue") : nullptr;
		// WBP_ModernMenu_MagicListView has no GetListView: its inner "ListView" (VModernListView) holds the ScrollBarStyle
		if (!inner) inner = ui::FindInTree(list, "ListView");
		if (!inner) inner = list;
		if (!reflect::Usable(inner)) return;
		unsigned count = 0;
		const unsigned resets = PaintScrollbarStyle(inner, "ScrollBarStyle", count);
		static std::set<std::string> s_logged;   // TEMP-DIAG: what each list's scrollbar holds (Magic thumb stayed pale); record in the KB
		const auto type = NameOf(inner->GetClass());
		if (s_logged.size() < 4 && s_logged.insert(NameOf(list->GetClass())).second) {
			static auto* style = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.ScrollBarStyle");
			static auto* brush = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.SlateBrush");
			const int styleOffset = reflect::Offset(inner->GetClass(), "ScrollBarStyle");
			const int thumbOffset = style ? reflect::Offset(style, "NormalThumbImage") : -1;
			const int resourceOffset = brush ? reflect::Offset(brush, "ResourceObject") : -1;
			std::string thumb = "?";
			if (styleOffset >= 0 && thumbOffset >= 0 && resourceOffset >= 0)
				if (auto** at = reflect::At<UE::UObject*>(inner, styleOffset + thumbOffset + resourceOffset); at && *at && reflect::Usable(*at))
					thumb = NameOf(*at);
			logger::warn("TEMP-DIAG MRMAGICDARK scrollbar {} (list {}) style@{} thumb={} calls={} resets={}", type,
				NameOf(list->GetClass()), styleOffset, thumb, count, resets);
		}
	}

	void WatchDetails(UE::UObject* menu, UE::UObject* list, const Target& target)
	{
		const std::string_view kDetails = target.detailsClass;
		auto* details = ui::FindByClassPrefix(menu, kDetails, 3000);
		UE::UObject* root = list;
		for (auto* owner = list; owner && reflect::Usable(owner); owner = owner->GetOuter())
			if (owner->GetClass()->IsChildOf(ui::Class(L"/Script/UMG.UserWidget"))) root = owner;
		if (!details && root && root != menu) details = ui::FindByClassPrefix(root, kDetails, 3000);
		if (!details) return;
		if (details != reflect::Get(g_details)) g_unhoveredRules.clear();
		g_details = reflect::Hold(details);
		g_detailsTarget = &target;
		static UE::UClass* effectLines = nullptr;
		if (!effectLines) {
			effectLines = ui::Class(L"/Game/UI/Legacy/GameMenuLayer/Commun/WBP_LegacyMenu_AddedEffects.WBP_LegacyMenu_AddedEffects_C");
			if (effectLines && !pe::WatchPost(effectLines, &OnEffectLineEvent)) effectLines = nullptr;
		}
		static UE::UClass* watched = nullptr;
		if (watched != details->GetClass() && pe::WatchPost(details->GetClass(), &OnDetailsUpdated))
			watched = details->GetClass();
		StyleDetails(details);
	}
}
