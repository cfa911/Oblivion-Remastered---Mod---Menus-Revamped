#pragma once

namespace sorting
{
	enum class Mode { None, Inventory, Magic, Container, SpellPurchase };
	// Inventory: one picker per tab. Magic: school, type and effect filter the same list together.
	void Tick(Mode a_mode, UE::UObject* a_menu, UE::UObject* a_canvas, float a_viewportScale, UE::UObject* a_other = nullptr);
	void Controller(std::uint16_t& buttons, short& leftX, short& leftY, bool connected);
	bool Active(int a_side = 0);
	// Re-hide native School/ItemCategory replacements immediately after a Magic menu update.
	void RefreshMagicHeaders(UE::UObject* a_menu);
	// RMI-047: follow a native Inventory tab change and re-hide the native sorts in the Inventory post-event.
	void RefreshInventoryHeaders(UE::UObject* a_menu);
	// RMI-047: the same for both container/barter sides, in the container menu's post-event.
	void RefreshContainerHeaders(UE::UObject* a_menu);
	const std::vector<UE::UObject*>& Evaluate(int side, const std::vector<UE::UObject*>& source, std::uint64_t generation);
	bool PopupOpen();
	bool Message(UINT a_message, WPARAM a_key, LPARAM a_position);
}
