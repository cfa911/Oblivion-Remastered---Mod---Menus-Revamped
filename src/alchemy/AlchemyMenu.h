#pragma once

// ============================================================================================================
// Alchemy menu (0x410, WBP_ModernMenu_AlchemyMenu_C): ingredient sort header + Weight/Value columns and controller
// layout (IMP-MR-005, RMI-027), counts on the effect
// filter rows (IMP-MR-004) and the active-filter line under the Ingredients title (IMP-MR-006).
// Layout and events: knowledge base docs/game-internals.md "Alchemy probe run 3/4" (build 1.512.105.0).
// Game thread only.
// ============================================================================================================

namespace alchemy
{
	void Tick();                                                   // every frame, from core::Tick
	bool Message(UINT a_message, WPARAM a_key, LPARAM a_position); // window messages; true = handled (not passed to the game)
	void Controller(std::uint16_t& a_buttons, short& a_lx, short& a_ly); // custom modes only; native X/Y pass through
	bool Open();
	UE::UObject* Widget();
	bool Naming(); // controller keyboard targets native potion-name field
	bool SearchAvailable(); // no other Alchemy input mode owns the keys
	bool SearchRect(UE::UObject* canvas, double& x, double& y, double& w, double& h, bool filters = false);
	UE::UObject* FilterWidget();
	bool FilterSearchAvailable();
}
