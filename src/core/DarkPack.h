#pragma once
// The colour packs (src/core/rules/PresetPacks.h): Dark ships Plugins/MenusRevamped/DarkPack/MRDark_P.* (default colours);
// Custom uses the Pack Builder's Plugins/MenusRevamped/CustomPack/MRCustom_P.* and its colour file
// (Plugins/MenusRevamped/CustomColours.ini), else Dark. The engine mounts Content/Paks/~mods after OBSE plugins load
// (MR-PAK-PROBE, game 1.512.105.0), so the pack is copied in at plugin load; it applies in the same launch.
#include <string_view>

namespace darkpack
{
	void Apply(std::string_view a_gameBuild);   // at OBSE_PLUGIN_LOAD, after the INI is read (a_gameBuild: "1.512.105.0")
	bool BakedLaurelTint();                      // the installed pack includes the Inventory laurel tint
	bool Active();                              // a colour pack is in ~mods for this launch (Dark or Custom): code that the pack replaces steps aside
	bool LayoutBaked();                         // the sizes are baked this launch (MRLayout_P, or MRCustom_P with sizes, in ~mods)
	bool HeaderLayoutBaked();
	bool HeaderOverflowBaked();
}
