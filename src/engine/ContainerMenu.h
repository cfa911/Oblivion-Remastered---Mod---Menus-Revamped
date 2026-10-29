#pragma once

#include "engine/Menus.h"

// Independent discovery of the shared container/barter widget; no SearchLight dependency.
namespace container
{
	void Tick();
	bool Open();
	UE::UClass* Class();
	UE::UObject* Widget();
	// Native menu chrome only (not dynamic item rows). Weak lookups reset on activation/close.
	UE::UObject* FindWidget(UE::UObject* a_owner, std::string_view a_name);
	UE::UObject* SideContent(int a_side);  // API menu IDs 3=player, 4=container/merchant
	inline UE::UObject* SideContent(menus::Menu a_side) { return SideContent(static_cast<int>(a_side)); }
	bool SideShown(int a_side);   // the side's content is on screen

	// While one lives (the plugin's tick), the menu's and each side's visibility - a parent walk of IsVisible calls - is
	// read once: nothing in a tick changes it (this plugin never hides or fades a native parent of theirs). Outside a tick
	// every call reads it fresh; a close (BP_OnDeactivated) is honoured at once either way.
	class TickScope
	{
	public:
		TickScope();
		~TickScope();
		TickScope(const TickScope&) = delete;
		TickScope& operator=(const TickScope&) = delete;
	};
	// RMI-004: called on the game thread, before the native body, when the Transfer action fires and when its sequence settles.
	void SetTransferHandlers(void (*a_started)(), void (*a_settled)());
}
