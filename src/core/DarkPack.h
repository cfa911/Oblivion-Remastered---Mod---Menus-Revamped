#pragma once
// The Custom UI pack (Plugins/MenusRevamped/CustomPack/MRCustom_P.pak/.utoc/.ucas + MRCustom_P.json, built by
// tools/make_dark_pack.py) and its colour file (Plugins/MenusRevamped/CustomColours.ini). The engine mounts Content/Paks/~mods
// after OBSE plugins load (MR-PAK-PROBE, game 1.512.105.0), so the pack is copied in at plugin load when uPreset=2 and removed
// otherwise; it applies in the same launch.
#include <string_view>

namespace darkpack
{
	void Apply(std::string_view a_gameBuild);   // at OBSE_PLUGIN_LOAD, after the INI is read (a_gameBuild: "1.512.105.0")
	bool BakedLaurelTint();                      // the installed pack includes the Inventory laurel tint
	bool Active();                              // the pack is in ~mods for this launch (Custom): code that the pack replaces steps aside
	bool LayoutBaked();                         // the sizes are baked this launch (MRLayout_P, or MRCustom_P with sizes, in ~mods)
	bool HeaderLayoutBaked();
	bool HeaderOverflowBaked();
}
