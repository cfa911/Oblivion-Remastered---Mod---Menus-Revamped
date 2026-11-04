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
		struct Alchemy
		{
			bool enabled = true;                 // [Alchemy] bEnabled: sort label, filter counts, active-filter line
			int  defaultSort = 0;                // uDefaultSort 0 Name, 1 Count, 2 Weight, 3 Value (4, the old Known effects, = Name)
			bool hideZeroCountEffects = false;   // bHideZeroCountEffects: hide (1) or grey out (0) effects no ingredient has
		} alchemy;
	};
	void Load();
	const Values& Get();
	std::filesystem::path PluginFolder();   // the folder the DLL is in (OBSE\Plugins)

	// One key of MenusRevamped.ini, clamped to [min, max]; a missing (or, for decimals, unparsable) key gives the default.
	int    ReadInt(const std::filesystem::path& a_ini, const wchar_t* a_section, const wchar_t* a_key, int a_default, int a_min, int a_max);
	double ReadDouble(const std::filesystem::path& a_ini, const wchar_t* a_section, const wchar_t* a_key, double a_default, double a_min, double a_max);
}
