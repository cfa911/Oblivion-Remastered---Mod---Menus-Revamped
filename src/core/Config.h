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
	std::filesystem::path PluginFolder();   // the folder the DLL is in (OBSE\Plugins)

	// One key of MenusRevamped.ini, clamped to [min, max]; a missing (or, for decimals, unparsable) key gives the default.
	int    ReadInt(const std::filesystem::path& a_ini, const wchar_t* a_section, const wchar_t* a_key, int a_default, int a_min, int a_max);
	double ReadDouble(const std::filesystem::path& a_ini, const wchar_t* a_section, const wchar_t* a_key, double a_default, double a_min, double a_max);
}
