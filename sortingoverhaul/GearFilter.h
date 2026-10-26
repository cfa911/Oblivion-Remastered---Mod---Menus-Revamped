#pragma once

namespace gearfilter
{
	using NameMatches = bool (*)(UE::UObject*, const std::string&);
	enum class Mode { None, Inventory, Magic, Container };
	// Inventory: one picker per tab. Magic: two pickers (school, effect) that filter the same list together.
	void Tick(Mode a_mode, UE::UObject* a_menu, UE::UObject* a_canvas, float a_viewportScale, UE::UObject* a_other = nullptr);
	void Controller(std::uint16_t& buttons, short& leftX, short& leftY, bool connected);
	bool Active(int a_side = 0);
	void Invalidate();  // native inventory/list rebuild, on the UE thread
	void Filter(UE::UObject* a_list, const std::vector<UE::UObject*>& a_live, const std::string& a_query, NameMatches a_nameMatches, int a_side = 0);
	bool Message(UINT a_message, WPARAM a_key, LPARAM a_position);
	bool Contains(double a_pixelX, double a_pixelY);
}
