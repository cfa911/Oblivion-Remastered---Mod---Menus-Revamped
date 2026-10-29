#pragma once
#include "search/SearchConfig.h"
#include "keyboard/KeyboardConfig.h"
#include "hover/HoverConfig.h"

namespace mrconfig
{
	struct Values
	{
		searchcfg::Values search;
		smoothcfg::Values smooth;
		kbcfg::Values keyboard;
		bool returnToListAfterFilter = true;
	};
	void Load();
	const Values& Get();
	std::filesystem::path PluginFolder();
}
