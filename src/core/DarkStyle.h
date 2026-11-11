#pragma once
#include <array>
// The Dark preset's colours, in one place (owner 2026-11-10): the Inventory and the Magic menu read the same values.
// sRGB 0-255, like the art recipes. A Dark PNG that the game colours is a pure white shape (stock alpha kept), drawn times
// the value here; baked art (pages, tabs) is made from these values by the tools/ scripts: re-run them after a change.
namespace darkstyle
{
	struct Rgb
	{
		float r, g, b;   // sRGB 0-255
	};

	struct Style
	{
		// List page, the installed Inventory panel art (MRSkinPlayerPanel_P T_UI_PlayerMenu_Background, 2026-11-10 export): a
		// tone curve on the stock brightness, <= 40 -> frame, >= 124 -> panel, linear between (the ornament bar keeps its
		// relief). MagicPage.png is baked with it by tools/make_dark_tab_art.py.
		Rgb panel{ 41, 41, 41 };
		Rgb frame{ 16, 16, 16 };
		// Details page: the game's own page widget tinted (linear, a multiply; sRGB about 25; black hides the parchment
		// completely). The Inventory page (M_MaskedPage) and the Magic page image use the same value.
		float detailsPage = 0.010f;
		Rgb rowText{ 239, 239, 239 };     // row names and values, details text (linear 0.86)
		Rgb rowSeparator{ 255, 255, 255 };// times RowSeparator.png (its own gray 235)
		Rgb columnLine{ 99, 99, 99 };     // row column lines T_Line_V (the Inventory pak's gray)
		Rgb rowIcon{ 239, 239, 239 };     // row icons drawn white and tinted brown by the game (school, stat icons)
		Rgb headerIcon{ 247, 247, 247 };  // header sort icons and the Magic School/Type All icons (the Inventory pak's white)
		Rgb pickerIcon{ 245, 245, 245 };  // picker icons (popups and header buttons): baked into assets/Sorting/DarkInventory by tools/make_dark_picker_icons.py
		// Tab strip greys (sRGB gray), the Inventory tab compiler's palette; baked into the tab pictures by
		// tools/make_dark_tab_art.py (two-tone art cannot be coloured at run time): run it again after a change.
		struct Tab
		{
			float selected = 41;    // selected tab and the strip body (BODY)
			float unselected = 33;  // unselected tab boxes (BOX)
			float gap = 24;         // between the unselected boxes (GAP)
			float outline = 134;    // tab outlines, like the header rules (LINE)
			float glyph = 242;      // selected tab glyph and arrows
			float glyphDim = 162;   // unselected glyphs and arrows
		} tab;
		// Inventory art baked into the Dark pack MRDark_P by tools/make_dark_pack.py (phase 2: the values of the old local
		// MRSkinPlayerPanel_P / MRSkinHighlight_P packs). Flat colours on the stock alpha unless noted; rebuild the pack after a
		// change. The scrollbar art is drawn in frame, the column lines (T_Line_V) in columnLine, the sort icons in headerIcon,
		// the tab atlases from tab.
		Rgb listHighlight{ 91, 91, 91 };      // T_List_highlight_V3, the hovered row band
		Rgb subTabDisc{ 96, 96, 96 };         // T_inv_subtabs_button_BG, the selected details sub-tab disc (shows 99 97 99)
		Rgb subTabCircle{ 72, 72, 72 };       // T_inv_subtabs_button_BG2, the sub-tab circle (the game tints it white)
		Rgb subTabIcon{ 228, 228, 228 };      // the sub-tab glyphs (overview, skill, enchantment; shows 231 227 231)
		Rgb pattern{ 150, 150, 150 };         // T_Inventory_pattern, the laurels beside the action button
		Rgb enchantedDot{ 66, 99, 153 };      // T_Icon_enchanted: a round dot (radius 14.5 px) replaces the stock art
		// T_inv_details_preview_frame_D: the stock brightness inverted, black -> light, white -> dark
		Rgb previewFrameLight{ 150, 150, 150 };
		Rgb previewFrameDark{ 30, 30, 30 };
		// Phase 3 bakes the Magic row school icons and details stat icons flat rowIcon and the tab atlases from tab into MRDark_P.
		// The blue "Can only be used once a day" line is a follow-up (RMI-086: its colour is a literal in the details blueprint).
		// Alchemy (owner 2026-11-11). The four parchments (ingredient page, potion paper, apparatus page, Filters panel; the
		// first is shared with Spellmaking, the last with Container, Recharge, Repair, Sigil Stone, Training, Help) are baked
		// neutral gray: the stock brightness range mapped onto parchmentDark..parchmentLight, grain kept (tools/make_dark_pack.py
		// "parchment"; a multiplied tan came out green and noisy).
		Rgb parchmentDark{ 8, 8, 8 };       // average about sRGB 25 (owner: as dark as round 1, gray, grain kept)
		Rgb parchmentLight{ 33, 33, 33 };
		// Alchemy colours baked into MRDark_P (stock alpha kept; tools/make_dark_pack.py): the effect boxes under the list
		// (T_Alchemy_Item_Default), the potion name field (quill, rule, text colour), the Filters hover box and diamond, and the
		// blueprint colours of the Filters row boxes (filterRow, filterSelected: plain boxes, patched in the .uexp). The Alchemy
		// header's lightning and category icon are baked in headerIcon, the effect names in rowText.
		Rgb effectBox{ 66, 99, 153 };
		Rgb nameBox{ 239, 239, 239 };
		Rgb filterRow{ 66, 66, 66 };         // the normal row box (stock: a brush-tinted solid box, no texture)
		Rgb filterHover{ 112, 112, 112 };    // the hover box (stock cream T_Alchemy_Filters_Highlighted): light text stays readable
		Rgb filterSelected{ 14, 22, 42 };
		Rgb filterMark{ 92, 140, 214 };
		// Alchemy frame (T_UI_Alchemy_Background), ingredient slots and pot (Custom pack builder Phase 6, "alchemy" recipe): the
		// stock bronze metal and green-grey stone recoloured separately, stock shading and alpha kept; defaults = the owner art's averages.
		Rgb alchemyMetal{ 56, 83, 118 };
		Rgb alchemyStone{ 22, 25, 31 };
		// Run-time tints of the Dark skin (uistyle::kDarkSkin): linear 0-1 as Slate takes them, alpha last.
		struct SkinTints
		{
			std::array<float, 4> rule{ 0.65f, 0.65f, 0.65f, 0.55f };     // header rules
			std::array<float, 4> guide{ 0.55f, 0.55f, 0.55f, 0.13f };    // header guides
			std::array<float, 4> ring{ 0.55f, 0.55f, 0.55f, 1 };         // the skill level ring
			std::array<float, 4> ornament{ 0.45f, 0.45f, 0.45f, 1 };     // T_StatsMenu_Line_R (white art)
			std::array<float, 4> pattern{ 1, 1, 1, 1 };                  // T_Inventory_pattern (baked gray: untinted)
			std::array<float, 4> band{ 0.41f, 0.65f, 1.0f, 1 };          // per-channel inverse of T_inv_details_effects_title_BG's brown
			std::array<float, 4> buttonFill{ 0.12f, 0.12f, 0.12f, 1 };   // action button fill and arrows
			float scrollTrack = 0.0052f, scrollBorder = 0.0052f, scrollInside = 0.0222f;   // scrollbar material (linear gray)
			std::array<float, 4> emphasisInk{ 0.125f, 0.120f, 0.125f, 1 };   // owner swatch, sRGB 99 97 99
			std::array<float, 4> buttonRule{ 0.83f, 0.83f, 0.83f, 1 };   // rule middles match the end pieces (ButtonLine.png, sRGB 235)
		} skin;
	};
#define DARKSTYLE_SKIN_TINTS 1

	inline constexpr Style kDefaults{};
	// The values in use: the compiled-in defaults until darkpack::Apply reads Plugins/MenusRevamped/CustomColours.ini at load
	// (src/core/rules/CustomColours.h). Set once at OBSE_PLUGIN_LOAD, before any menu opens.
	namespace detail
	{
		inline Style    g_style = kDefaults;
		inline unsigned g_generation = 0;
	}
	inline const Style& Get() { return detail::g_style; }
	inline void         Set(const Style& a_style) { detail::g_style = a_style; ++detail::g_generation; }
	inline unsigned     Generation() { return detail::g_generation; }   // changes with every Set (uistyle::SkinFor rebuilds the Dark skin)

	// sRGB 0-255 to linear 0-1 (what Slate colours take), constexpr: x^2.4 = x^2 * fifth root of x^2.
	inline constexpr float Root5(float v)
	{
		if (v <= 0.0f) return 0.0f;
		float r = v > 1.0f ? v : 1.0f;
		for (int i = 0; i < 60; ++i) r = r - (r * r * r * r * r - v) / (5.0f * r * r * r * r);
		return r;
	}
	inline constexpr float Linear(float srgb)
	{
		const float c = srgb / 255.0f;
		if (c <= 0.04045f) return c / 12.92f;
		const float x = (c + 0.055f) / 1.055f;
		return x * x * Root5(x * x);
	}
	struct Linear4
	{
		float rgba[4];
		const float* data() const { return rgba; }
	};
	inline constexpr std::array<float, 4> Arr(const Rgb& c, float alpha = 1.0f) { return { Linear(c.r), Linear(c.g), Linear(c.b), alpha }; }
	inline constexpr Linear4 Lin(const Rgb& c, float alpha = 1.0f) { return { { Linear(c.r), Linear(c.g), Linear(c.b), alpha } }; }

#define DARKSTYLE_TABS_AND_DETAIL 1
#define DARKSTYLE_INVENTORY_PAGE 1

	// art swaps name the value they are drawn with (a member, so an override of Get() is picked up)
	using Value = Rgb Style::*;
#define DARKSTYLE_TINTED_ART 1
}
