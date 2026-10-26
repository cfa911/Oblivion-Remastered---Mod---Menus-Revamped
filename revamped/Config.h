#pragma once
#include "../menusearch/Config.h"
#include "../searchkb/Config.h"

namespace mrconfig
{
	struct Values
	{
		searchcfg::Values search;
		kbcfg::Values keyboard;
		bool returnToListAfterFilter = true;
	};
	void Load();
	const Values& Get();
	std::filesystem::path PluginFolder();
}
