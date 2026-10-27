#pragma once

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
	// RMI-004: called on the game thread, before the native body, when the Transfer action fires and when its sequence settles.
	void SetTransferHandlers(void (*a_started)(), void (*a_settled)());
}
