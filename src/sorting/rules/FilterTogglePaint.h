#pragma once
// Header stolen/enchanted toggles. The Inventory dark skin inverts them (owner, 2026-11-09): on = the icon in the row
// marker's colour (red hand, blue orb) without a coloured fill; off = the dimmed light icon. Other menus keep the
// coloured fill behind a light icon. Controller focus keeps the shared focus fill in both.
namespace filtertoggle
{
	inline constexpr float kClear[4]{ 0, 0, 0, 0 };
	inline constexpr float kLight[4]{ 1, 1, 1, 1 };
	// the enchanted row marker (T_Icon_enchanted override, sRGB 66/99/153) in linear colour
	inline constexpr float kEnchantedMark[4]{ 0.0545f, 0.1248f, 0.3185f, 1.0f };

	struct Paint { const float* fill; const float* icon; float opacity; };

	inline Paint Choose(bool a_inventory, bool a_on, bool a_focus, const float* a_focusFill, const float* a_onFill,
		const float* a_mark, float a_inactiveOpacity)
	{
		if (a_inventory)
			return { a_focus ? a_focusFill : kClear, a_on ? a_mark : kLight, a_on ? 1.0f : a_inactiveOpacity };
		return { a_focus ? a_focusFill : a_on ? a_onFill : kClear, kLight, a_on ? 1.0f : a_inactiveOpacity };
	}
}
