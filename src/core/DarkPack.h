#pragma once
// The Dark UI pack (assets/DarkPack/MRDark_P.pak/.utoc/.ucas, built by tools/make_dark_pack.py): baked Dark textures.
// The engine mounts Content/Paks/~mods after OBSE plugins load (MR-PAK-PROBE, game 1.512.105.0), so the pack is copied in at
// plugin load when the preset is Dark and removed otherwise; it applies in the same launch.
namespace darkpack
{
	void Apply();    // at OBSE_PLUGIN_LOAD, after the INI is read
	bool Active();   // the pack is in ~mods for this launch (Dark): code that the pack replaces steps aside
}
