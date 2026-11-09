// The native menu skin passes (see MenuSkin.h). Logic only: every colour and art file comes from uistyle::Skin.
#include "sorting/MenuSkin.h"
#include "sorting/rules/DetailsSkin.h"
#include "core/Config.h"
#include "engine/Reflect.h"
#include "engine/PEHook.h"
#include "engine/UiText.h"
#include "engine/DataTableRows.h"
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

		std::array<float, 4> WithAlpha(const uistyle::Color& c, float alpha) { return { c[0], c[1], c[2], alpha }; }

		std::string NameOf(UE::UObject* object) { return object ? pe::Utf8(object->GetFName().ToString()) : std::string("-"); }

		// TEMP-DIAG: one log of every image and text under a widget (name, brush resource, colour, render opacity), once per
		// tag. Measures the parts still drawn in stock colours before restyling them (rows, buttons, bands, ring, effects).
		void Census(const std::string& tag, UE::UObject* root, int maxLines)
		{
			static std::set<std::string> done;
			if (!reflect::Usable(root) || !done.insert(tag).second) return;
			static auto* imageClass = ui::Class(L"/Script/UMG.Image");
			static auto* textClass = ui::Class(L"/Script/UMG.TextBlock");
			static auto* richClass = ui::Class(L"/Script/UMG.RichTextBlock");
			int lines = 0;
			logger::warn("TEMP-DIAG MR census {}: root {} ({})", tag, NameOf(root), NameOf(root->GetClass()));
			for (auto* widget : ui::TreeWidgets(root, 3000)) {
				if (lines >= maxLines) break;
				if (!reflect::Usable(widget)) continue;
				auto* type = widget->GetClass();
				const bool image = imageClass && type->IsChildOf(imageClass);
				const bool text = (textClass && type->IsChildOf(textClass)) || (richClass && type->IsChildOf(richClass));
				const auto typeName = NameOf(type);
				if (!image && !text && typeName.find("Button") == std::string::npos) continue;
				float rgba[4]{ -1, -1, -1, -1 };
				ReadColor(widget, rgba);
				logger::warn("TEMP-DIAG MR census {}: {} {} res {} rgba {:.3f} {:.3f} {:.3f} {:.2f} op {:.2f} shown {}", tag, typeName,
					NameOf(widget), image ? NameOf(ui::BrushResource(widget)) : std::string("-"), rgba[0], rgba[1], rgba[2], rgba[3],
					ui::RenderOpacity(widget), ui::ShownOnScreen(widget));
				++lines;
			}
		}

		// TEMP-DIAG: the images and texts actually on screen under a widget, with their viewport positions, once per tag.
		// Matches what the owner points at (gold ornament lines, brown button rules) to a widget and its resource.
		void CensusShown(const std::string& tag, UE::UObject* root, int maxLines)
		{
			static std::set<std::string> done;
			if (!reflect::Usable(root) || done.contains(tag)) return;   // the details root never reports shown: filter per image
			static auto* imageClass = ui::Class(L"/Script/UMG.Image");
			static auto* textClass = ui::Class(L"/Script/UMG.TextBlock");
			int lines = 0;
			for (auto* widget : ui::TreeWidgets(root, 3000)) {
				if (lines >= maxLines) break;
				if (!reflect::Usable(widget) || !ui::ShownOnScreen(widget)) continue;
				const bool image = imageClass && widget->GetClass()->IsChildOf(imageClass);
				if (!image && !(textClass && widget->GetClass()->IsChildOf(textClass))) continue;
				double x = 0, y = 0, w = 0, h = 0;
				ui::Measure(widget, x, y, w, h);
				float rgba[4]{ -1, -1, -1, -1 };
				ReadColor(widget, rgba);
				logger::warn("TEMP-DIAG MR shown {}: {} {} at {:.0f},{:.0f} {:.0f}x{:.0f} rgba {:.3f} {:.3f} {:.3f} {:.2f}", tag, NameOf(widget),
					image ? NameOf(ui::BrushResource(widget)) : std::string("text"), x, y, w, h, rgba[0], rgba[1], rgba[2], rgba[3]);
				++lines;
			}
			if (lines) done.insert(tag);   // logged once something was on screen
		}

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
		bool SwapArt(UE::UObject* image, const uistyle::Skin& skin)
		{
			if (!skin.artSwapCount || !reflect::Usable(image)) return false;
			const auto resource = NameOf(ui::BrushResource(image));
			for (int i = 0; i < skin.artSwapCount; ++i) {
				if (resource != skin.artSwaps[i].stock) continue;
				auto* art = SkinArt(skin.artSwaps[i].file);
				if (!art) return false;
				ui::Call set(image, L"SetBrushResourceObject");
				if (!set.Set("ResourceObject", art) || !set.RunGuarded()) return false;
				float rgba[4];
				if (ReadColor(image, rgba)) ui::Colour(image, 1.0f, 1.0f, 1.0f, rgba[3]);
				return true;
			}
			// already swapped (a later pass over the same widget): the brush draws one of our own textures
			for (int i = 0; i < skin.artSwapCount; ++i)
				if (auto* art = SkinArt(skin.artSwaps[i].file); art && art == ui::BrushResource(image)) return true;
			return false;
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
			const bool scrolled = ui::CallFirst(label, L"SetScrollingEnabled", &off, sizeof(off));
			const bool widened = bool(ui::Call(label, L"SetMinDesiredWidth"));
			if (widened) ui::Float(label, L"SetMinDesiredWidth", kTitleWidth);
			// The title sits in a RetainerBox (inv_cont_AnimationRetainerforText) that draws it through an effect material for
			// its reveal animation; that mask cut the text at a fixed width whatever the text's own width (owner screenshots on
			// 7c938a78 and 76e583e0). Drawn directly, the whole name shows.
			static auto* retainerClass = ui::Class(L"/Script/UMG.RetainerBox");
			std::string chain;
			UE::UObject* parent = title;
			for (int depth = 0; depth < 4 && parent && reflect::Usable(parent); ++depth) {
				double x = 0, y = 0, w = 0, h = 0;
				ui::Measure(parent, x, y, w, h);
				std::string effect;
				if (retainerClass && parent->GetClass()->IsChildOf(retainerClass)) {
					if (const int at = reflect::Offset(parent->GetClass(), "EffectMaterial"); at >= 0)
						if (auto** material = reflect::At<UE::UObject*>(parent, at)) effect = NameOf(*material);
					const bool retain = false;
					ui::CallFirst(parent, L"SetRetainRendering", &retain, sizeof(retain));
				}
				chain += std::format(" | {} w {:.0f}{}", NameOf(parent), w, effect.empty() ? "" : " effect " + effect);
				ui::Call up(parent, L"GetParent");
				parent = up && up.Run() ? up.Get<UE::UObject*>("ReturnValue") : nullptr;
			}
			static int reported = 0;
			if (reported < 3) {
				++reported;
				double x = 0, y = 0, w = 0, h = 0;
				ui::Measure(label, x, y, w, h);
				logger::warn("TEMP-DIAG MR native skin: title text {} w {:.0f} scrolling off {} widened {}{}", NameOf(label), w, scrolled, widened, chain);
			}
			for (auto* widget : { label, title })
				if (ui::Call prepass(widget, L"ForceLayoutPrepass"); widget && prepass) prepass.Run();
		}

		// ---- item details ----
		reflect::Handle g_details;
		const Target* g_detailsTarget = &kInventory;

		// the action button (Equip, Use...): its fill and arrows gray, its label light; alpha stays the game's (hover fades)
		void StyleDetailsButton(UE::UObject* button)
		{
			const auto& skin = Current();
			static auto* imageClass = ui::Class(L"/Script/UMG.Image");
			static auto* textClass = ui::Class(L"/Script/UMG.TextBlock");
			if (!skin.recolor || !imageClass || !textClass) return;
			Census("button", button, 40);
			for (auto* widget : ui::TreeWidgets(button, 60)) {
				if (!reflect::Usable(widget)) continue;
				float rgba[4];
				if (!ReadColor(widget, rgba)) continue;
				if (widget->GetClass()->IsChildOf(imageClass)) {
					if (SwapArt(widget, skin)) continue;   // arrows and rules
					const auto fill = WithAlpha(skin.buttonFill, rgba[3]);
					ui::Colour(widget, fill[0], fill[1], fill[2], fill[3]);
				} else if (widget->GetClass()->IsChildOf(textClass)) {
					const auto ink = WithAlpha(skin.ink, rgba[3]);
					uix::Ink(widget, ink.data());
				}
			}
		}

		void OnDetailsButtonEvent(UE::UObject* button, UE::UFunction* function, void*)
		{
			auto* details = reflect::Get(g_details);
			if (!details || !reflect::Usable(button) || !ui::IsWithin(button, details)) return;
			// TEMP-DIAG: the colours the button's own code leaves (rules stayed brown on 76e583e0), per event, a few times
			static int measured = 0;
			if (measured < 4 && ui::ShownOnScreen(button)) {
				++measured;
				Census("button after " + std::string(pe::FunctionName(function)), button, 40);
				CensusShown("details", details, 140);   // the whole panel as it is on screen (ornaments, button rules)
			}
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
			Census("effect entry " + NameOf(entry->GetClass()), entry, 30);
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
		bool RebuildEmphasis(UE::UObject* rich, const uistyle::Skin& skin)
		{
			static auto* tableClass = ui::Class(L"/Script/Engine.DataTable");
			static auto* textStyle = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.TextBlockStyle");
			if (skin.emphasisInk[3] <= 0 || !tableClass || !textStyle) return false;
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
				if (!Readable(color, 16) || !detailsskin::IsWarmInk(color)) return;
				UE::FName name;
				static_assert(sizeof(name) == sizeof(key));
				std::memcpy(&name, &key, sizeof(key));
				// bold rows are the highlighted words (emphasis colour); the other brown rows are normal text (perk ink)
				const auto rowName = pe::Utf8(name.ToString());
				const bool bold = detailsskin::IsEmphasisRow(rowName);
				static int rowsLogged = 0;   // TEMP-DIAG: which rows are brown (bold rows were not recognised on 0d025571)
				if (rowsLogged < 8) {
					++rowsLogged;
					logger::warn("TEMP-DIAG MR native skin: perk row {} rgba {:.3f} {:.3f} {:.3f} bold {}", rowName, color[0], color[1], color[2], bold);
				}
				saved.push_back({ color, { color[0], color[1], color[2], color[3] } });
				std::copy_n((bold ? skin.emphasisInk : skin.perkInk).data(), 3, color);
			});
			if (saved.empty()) return false;
			UE::UObject* none = nullptr;
			ui::CallFirst(rich, L"SetTextStyleSet", &none, sizeof(none));
			ui::CallFirst(rich, L"SetTextStyleSet", &table, sizeof(table));
			for (auto& [color, original] : saved) std::copy_n(original.data(), 4, color);
			static bool reported = false;
			if (!reported) {
				reported = true;
				logger::warn("TEMP-DIAG MR native skin: perk emphasis {} rows recoloured in {} for the Inventory only", saved.size(), NameOf(table));
			}
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
			unsigned pages = 0, tints = 0, inks = 0, buttons = 0, levels = 0, perkInks = 0, barCalls = 0, barResets = 0;
			const auto tint = [&](UE::UObject* widget, const float* c) { ui::Colour(widget, c[0], c[1], c[2], c[3]); ++tints; };
			Census("details", details, 120);
			if (ui::ShownOnScreen(details)) Census("details shown", details, 160);   // colours after the game set them
			CensusShown("details", details, 120);
			for (auto* widget : ui::TreeWidgets(details, 3000)) {
				if (!reflect::Usable(widget)) continue;
				auto* type = widget->GetClass();
				const auto typeName = NameOf(type);
				if (typeName.find("Button") != std::string::npos && typeName.starts_with("WBP_ModernPrefab")) {
					pe::WatchPost(type, &OnDetailsButtonEvent);
					StyleDetailsButton(widget); ++buttons;
					continue;
				}
				if (typeName.find("Entry") != std::string::npos && typeName.ends_with("_C")) {   // effect lines and other entries
					pe::WatchPost(type, &OnDetailsEntryEvent);
					StyleDetailsEntry(widget);
					continue;
				}
				if (typeName.starts_with("WBP_ModernMenu_Stats_LevelDescription")) {
					perkInks += StyleLevelDescription(widget); ++levels;
					continue;
				}
				if (typeName.starts_with("WBP_OriginalMenu_InventoryItemSpecificContent") ||
					typeName.starts_with("WBP_ModernMenu_StatsPage3_Details"))
					Census(typeName, widget, 80);
				if (listViewClass && type->IsChildOf(listViewClass)) {   // effect lines and other generated entries
					StyleListEntries(widget);
					continue;
				}
				if (scrollBoxClass && type->IsChildOf(scrollBoxClass)) {   // the skill sub-tab's perk list
					barResets += PaintScrollbarStyle(widget, "WidgetBarStyle", barCalls);
					continue;
				}
				if (type->IsChildOf(imageClass)) {
					if (SwapArt(widget, skin)) { ++tints; continue; }
					auto* resource = ui::BrushResource(widget);
					if (!resource || !reflect::Usable(resource)) continue;
					std::string parent;
					if (parentOffset >= 0 && resource->GetClass()->IsChildOf(instanceClass))
						if (auto** at = reflect::At<UE::UObject*>(resource, parentOffset); at && *at && reflect::Usable(*at))
							parent = NameOf(*at);
					switch (detailsskin::Classify(NameOf(resource), parent)) {
					case detailsskin::Role::Page: {
						// keeps M_MaskedPage (its shape leaves the 3D preview open); the preset's gray hides the parchment
						float rgba[4];
						if (!ReadColor(widget, rgba)) break;
						detailsskin::PageTint(rgba, skin.pageGray);
						tint(widget, rgba); ++pages;
						break;
					}
					case detailsskin::Role::Ornament: {
						static int logged = 0;   // TEMP-DIAG: what the game tinted the white ornament art (brown-gold on screen)
						if (logged < 2 && ui::ShownOnScreen(widget)) {
							++logged;
							float rgba[4]{};
							ReadColor(widget, rgba);
							logger::warn("TEMP-DIAG MR native skin: ornament before tint {} {} rgba {:.3f} {:.3f} {:.3f} {:.2f}", NameOf(widget),
								NameOf(resource), rgba[0], rgba[1], rgba[2], rgba[3]);
						}
						tint(widget, skin.ornament.data());
						break;
					}
					case detailsskin::Role::Pattern: tint(widget, skin.pattern.data()); break;
					case detailsskin::Role::Band: tint(widget, skin.band.data()); break;
					case detailsskin::Role::Ring: {   // the level ring: a dynamic instance of M_UI_CircularProgess
						ui::Call set(resource, L"SetVectorParameterValue");
						if (set.Set("ParameterName", UE::FName(L"Color", UE::EFindName::Add)) && set.Set("Value", skin.ring) && set.RunGuarded())
							++tints;
						break;
					}
					default: break;
					}
				} else if (colorOffset >= 0 && type->IsChildOf(textClass)) {
					const auto* rgba = reflect::At<float>(widget, colorOffset);
					if (!rgba || !detailsskin::IsDarkInk(rgba)) continue;
					const auto ink = WithAlpha(skin.ink, rgba[3]);
					uix::Ink(widget, ink.data()); ++inks;
				}
			}
			static bool reported = false;
			if (!reported && pages + tints + inks) {
				reported = true;
				logger::warn("TEMP-DIAG MR native skin: details view {} pages, {} tints, {} inks, {} buttons", pages, tints, inks, buttons);
			}
			static bool skillReported = false;
			if (!skillReported && levels + barCalls) {
				skillReported = true;
				logger::warn("TEMP-DIAG MR native skin: skill sub-tab {} perk lines, {} perk inks, {} bar color calls, {} handle resets",
					levels, perkInks, barCalls, barResets);
			}
		}

		void OnDetailsUpdated(UE::UObject* details, UE::UFunction* function, void*)
		{
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

		// the prefab re-applies its variables on its own updates (SetText when the tab changes): keep the title whole after
		void OnTitleEvent(UE::UObject* widget, UE::UFunction* function, void*)
		{
			if (widget != reflect::Get(g_title)) return;
			const auto name = pe::FunctionName(function);
			if (name == "SetText" || name == "PreConstruct" || name == "Construct" || name == "ToggleScrollingText") UnclipTitle(widget);
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

	void StyleRow(UE::UObject* row, const Target& target)
	{
		const auto& skin = Current();
		if (!reflect::Usable(row)) return;
		const int equipped = reflect::Offset(row->GetClass(), "bIsEquiped");
		const auto* isEquipped = equipped >= 0 ? reflect::At<bool>(row, equipped) : nullptr;
		Census("row", row, 40);
		if (isEquipped && *isEquipped) Census("equipped row", row, 40);
		if (!skin.recolor) return;
		if (auto* name = RowColumn(row, target.rowName)) uix::Ink(name, skin.ink.data());
		for (int i = 0; i < target.rowValueCount; ++i)
			if (auto* label = ui::FindInTree(RowColumn(row, target.rowValues[i]), "inv_entry_value")) uix::Ink(label, skin.ink.data());
		// the row's art (separator line T_List_Default, shared by twelve menus; equipped band T_List_equip_V2, the RMI-072
		// line) swapped for the preset's on this row only: Slate tints cannot brighten or neutralise baked brown
		static auto* imageClass = ui::Class(L"/Script/UMG.Image");
		if (imageClass)
			for (auto* widget : ui::TreeWidgets(row, 80))
				if (reflect::Usable(widget) && widget->GetClass()->IsChildOf(imageClass)) SwapArt(widget, skin);
		// the equipped pen scribble (M_UI_Scribble) on dark rows
		if (skin.hideEquippedScribble)
			if (auto* scribble = ui::FindInTree(row, "inv_entry_scribble")) ui::Colour(scribble, 1.0f, 1.0f, 1.0f, 0.0f);
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
		static bool reported = false;
		ui::Call get(list, L"GetListView");
		auto* inner = get.Size("ReturnValue") == sizeof(UE::UObject*) && get.RunGuarded()
			? get.Get<UE::UObject*>("ReturnValue") : list;
		if (!reflect::Usable(inner)) return;
		unsigned count = 0;
		const unsigned resets = PaintScrollbarStyle(inner, "ScrollBarStyle", count);
		if (!reported) {
			reported = true;
			logger::warn("TEMP-DIAG MR native skin: scrollbar style size {} and {} color calls, {} handle resets",
				ui::SizeOf(inner->GetClass(), "ScrollBarStyle"), count, resets);
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
