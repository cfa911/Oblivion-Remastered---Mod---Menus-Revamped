#pragma once
#include "search/WorldMapRules.h"
namespace worldmap
{
	void Tick();
	bool Open();
	UE::UObject* Widget();
	UE::UObject* View();
	std::uint64_t Generation();
	std::uint32_t ActiveFilterMask();
	bool CopyFilterIcon(int type, UE::UObject* image);
	std::vector<maprules::Location> Snapshot();
	bool Focus(const std::string& id);
	bool Position(double& x, double& y, float& zoom);
	bool DisplayedCenter(double& x, double& y);
}
