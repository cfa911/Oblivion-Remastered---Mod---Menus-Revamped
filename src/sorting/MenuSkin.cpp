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
		UnclipTitle(title);   // decision 28: the title colour is baked (inv_cont_title, ContainerInventory_MainContent)
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

	void InkText(UE::UObject* label)
	{
		const auto& skin = Current();
		if (skin.recolor && reflect::Usable(label)) uix::Ink(label, skin.ink.data());
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
}
