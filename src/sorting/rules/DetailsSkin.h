#pragma once
// Dark Inventory skin for the item details view (RMI-071). The page material's texture (T_SpellPurchase_LeftPageBG) and the
// ornament line (T_StatsMenu_Line_R) are shared with other menus, so they are restyled per widget here rather than replaced
// in the texture package; Inventory-only textures (preview frame and background) come from the package.
#include <algorithm>
#include <string_view>
namespace detailsskin
{
#define DETAILSSKIN_ROUND6 1
	enum class Role { None, Page, Ornament, Pattern, Band, Ring };

	// a_resource: the name of the image brush's resource; a_parent: the parent material's name when it is a material instance
	inline Role Classify(std::string_view a_resource, std::string_view a_parent)
	{
		const auto is = [&](std::string_view prefix) { return a_resource.starts_with(prefix) || a_parent.starts_with(prefix); };
		if (is("MIC_MaskedPage")) return Role::Page;
		if (is("MIC_UI_SkillCircularProgress") || is("M_UI_CircularProgess")) return Role::Ring;
		if (a_resource == "T_StatsMenu_Line_R" || a_resource == "T_decoration_line") return Role::Ornament;
		if (a_resource == "T_Inventory_pattern") return Role::Pattern;
		if (a_resource == "T_inv_details_effects_title_BG") return Role::Band;
		return Role::None;
	}

	// a linear text colour that is dark and nearly gray (the parchment ink); saturated colours carry meaning and are kept
	inline bool IsDarkInk(const float* a_rgba)
	{
		const float hi = std::max({ a_rgba[0], a_rgba[1], a_rgba[2] });
		const float lo = std::min({ a_rgba[0], a_rgba[1], a_rgba[2] });
		return hi < 0.2f && hi - lo < 0.1f;
	}

	// Colours live in uistyle::Skin (core/UiTheme.h), per preset; this header only decides what each widget is.
	// The sub-tab circle and shine (T_inv_subtabs_button_BG2/BG, Inventory-only) are darkened in the texture package:
	// the button's animation resets widget tints on selection, so a tint never held (white disc, round 4).

	// Round 3 (owner screenshot on ebf55066)
#define DETAILSSKIN_ROUND3 1
	// The page keeps M_MaskedPage: its SDF shape leaves the 3D item preview open, a flat box covered it and spilled over
	// the list. The material has no texture or colour parameter, so only the widget tint darkens it, Inventory-only (owner
	// choice 2026-11-09; the shared T_SpellPurchase_LeftPageBG stays stock). Black hides the parchment completely; this very
	// dark gray (sRGB about 25, the list is 41) keeps it faint (round 4 test, owner to judge).
	inline constexpr bool kPageKeepsMaterial = true;
	inline void PageTint(float* a_rgba, float a_gray) { a_rgba[0] = a_rgba[1] = a_rgba[2] = a_gray; }

	// A rich-text style row that draws the highlighted words (perk descriptions): the bold styles. Owner screenshot on
	// 89d7ca0d: only these take the emphasis colour; Regular-Regular and Default are the normal text.
#define DETAILSSKIN_EMPHASIS_ROWS 1
	// log on f018ac87: the brown rows are Default (normal text) and RT_BlueMed (the highlighted words, brown in this game)
	inline bool IsEmphasisRow(std::string_view a_row) { return a_row == "RT_BlueMed" || a_row.find("Bold") != std::string_view::npos; }

	// a warm brown text colour (the locked-perk lines of the skill sub-tab); grays and saturated colours are not warm
	inline bool IsWarmInk(const float* a_rgba)
	{
		const float hi = std::max({ a_rgba[0], a_rgba[1], a_rgba[2] });
		const float lo = std::min({ a_rgba[0], a_rgba[1], a_rgba[2] });
		return hi > 0.0f && hi < 0.6f && a_rgba[0] >= a_rgba[1] && a_rgba[1] >= a_rgba[2] &&
		       a_rgba[0] - a_rgba[2] > 0.03f && (hi - lo) / hi < 0.9f;
	}

	// Brush tints (census on a593c0f7): a colour-only image draws Brush.TintColor x ColorAndOpacity. The action button's
	// rule middles carry the stock brown 0.070/0.038/0.028 (the fill a white one); the ornament lines a gold 0.25/0.18/0.11.
#define DETAILSSKIN_BRUSH_TINT 1
	inline bool IsBrushRule(const float* a_brush) { return a_brush[3] > 0.0f && IsDarkInk(a_brush) && a_brush[0] > a_brush[2]; }
	inline void NeutralBrush(float* a_brush, float a_level) { a_brush[0] = a_brush[1] = a_brush[2] = a_level; }
}
