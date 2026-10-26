#pragma once

// Independent discovery of the shared container/barter widget; no SearchLight dependency.
namespace container
{
	void Tick();
	bool Open();
	UE::UClass* Class();
	UE::UObject* Widget();
	UE::UObject* SideContent(int a_side);  // API menu IDs 3=player, 4=container/merchant
}
