#pragma once
#include "../sortingoverhaul/Container.h"
#include "Menus.h"
namespace container {
	inline UE::UObject* SideContent(menus::Menu side) { return SideContent(static_cast<int>(side)); }
}
