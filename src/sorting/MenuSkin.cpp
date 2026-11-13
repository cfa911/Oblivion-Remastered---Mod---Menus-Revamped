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
