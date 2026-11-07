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
		int  stickDeadzone = 16000;           // [Controller] uStickDeadzone: left stick travel (of 32767) before it counts as a push
		struct Picker
		{
			bool inventory = true, magic = true, container = true;   // [Picker] bInventory / bMagic / bContainer: category pickers per menu
			bool stolenFilter = true, enchantedFilter = true;          // bStolenFilter / bEnchantedFilter: the toggle icons
			bool categorySort = true;                                  // bCategorySort: lists start grouped by the category filter's order
		} picker;
		struct Alchemy
		{
			bool enabled = true;                 // [Alchemy] bEnabled: sort label, filter counts, active-filter line
			int  defaultSort = 0;                // uDefaultSort 0 Name, 1 Count, 2 Weight, 3 Value (4, the old Known effects, = Name)
			int  markerDiameter = 18;            // uMarkerDiameter: effect circle size, 8-20 (owner 2026-11-05: 30% over 14)
		} alchemy;
	};
	void Load();
	const Values& Get();
	std::filesystem::path PluginFolder();   // the folder the DLL is in (OBSE\Plugins)

	// One key of MenusRevamped.ini, clamped to [min, max]; a missing (or, for decimals, unparsable) key gives the default.
	int    ReadInt(const std::filesystem::path& a_ini, const wchar_t* a_section, const wchar_t* a_key, int a_default, int a_min, int a_max);
	double ReadDouble(const std::filesystem::path& a_ini, const wchar_t* a_section, const wchar_t* a_key, double a_default, double a_min, double a_max);
}
