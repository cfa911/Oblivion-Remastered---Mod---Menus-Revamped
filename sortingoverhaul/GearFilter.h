#pragma once

namespace gearfilter
{
	using NameMatches = bool (*)(UE::UObject*, const std::string&);
	enum class Mode { None, Inventory, Magic };
	// Inventory: one picker per tab. Magic: two pickers (school, effect) that filter the same list together.
	void Tick(Mode a_mode, UE::UObject* a_menu, UE::UObject* a_canvas, float a_viewportScale);
	bool Active();
	void Invalidate();  // native inventory/list rebuild, on the UE thread
	void Filter(UE::UObject* a_list, const std::vector<UE::UObject*>& a_live, const std::string& a_query, NameMatches a_nameMatches);
	bool Message(UINT a_message, WPARAM a_key, LPARAM a_position);
	bool Contains(double a_pixelX, double a_pixelY);
}
