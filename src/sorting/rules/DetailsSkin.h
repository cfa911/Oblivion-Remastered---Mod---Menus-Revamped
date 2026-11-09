#pragma once
// Dark Inventory skin for the item details view (RMI-071). The page material's texture (T_SpellPurchase_LeftPageBG) and the
// ornament line (T_StatsMenu_Line_R) are shared with other menus, so they are restyled per widget here rather than replaced
// in the texture package; Inventory-only textures (preview frame and background) come from the package.
#include <algorithm>
#include <string_view>
namespace detailsskin
{
	enum class Role { None, Page, Ornament, Pattern, Band, ButtonBack };

	// a_resource: the name of the image brush's resource; a_parent: the parent material's name when it is a material instance
	inline Role Classify(std::string_view a_resource, std::string_view a_parent)
	{
		const auto is = [&](std::string_view prefix) { return a_resource.starts_with(prefix) || a_parent.starts_with(prefix); };
		if (is("MIC_MaskedPage")) return Role::Page;
		if (a_resource == "T_StatsMenu_Line_R") return Role::Ornament;
		if (a_resource == "T_Inventory_pattern") return Role::Pattern;
		if (a_resource == "T_inv_details_effects_title_BG") return Role::Band;
		if (a_resource == "T_inv_subtabs_button_BG2") return Role::ButtonBack;
		return Role::None;
	}

	// a linear text colour that is dark and nearly gray (the parchment ink); saturated colours carry meaning and are kept
	inline bool IsDarkInk(const float* a_rgba)
	{
		const float hi = std::max({ a_rgba[0], a_rgba[1], a_rgba[2] });
		const float lo = std::min({ a_rgba[0], a_rgba[1], a_rgba[2] });
		return hi < 0.2f && hi - lo < 0.1f;
	}

	inline constexpr float kPageFill[]{ 0.022f, 0.021f, 0.022f, 1.0f };   // sRGB 41: the list panel
	inline constexpr float kPageEdge[]{ 0.65f, 0.65f, 0.65f, 0.55f };      // the header rule ink
	inline constexpr float kOrnament[]{ 0.45f, 0.45f, 0.45f, 1.0f };
	// T_Inventory_pattern (the laurels) ships gray in the texture package: shown untinted
	inline constexpr float kPatternTint[]{ 1.0f, 1.0f, 1.0f, 1.0f };
	// T_inv_details_effects_title_BG has brown baked in (about 159/128/105) and is shared with the Stats skill page:
	// the per-channel inverse of its linear colour turns it gray in the Inventory only
	inline constexpr float kBandTint[]{ 0.41f, 0.65f, 1.0f, 1.0f };
	// The sub-tab circle (white art): the game's own tint scaled down, so the selected button stays the brighter one.
	// False, unchanged, when the tint is already dark (a pass that already ran).
	inline bool ScaleTint(float* a_rgba)
	{
		if (std::max({ a_rgba[0], a_rgba[1], a_rgba[2] }) < 0.2f) return false;
		for (int i = 0; i < 3; ++i) a_rgba[i] *= 0.07f;
		return true;
	}
	inline constexpr float kInk[]{ 0.86f, 0.86f, 0.86f, 1.0f };            // the list's light ink
}
