#pragma once
// In-game Pack Builder window contents (Phase 2: the builder UI, a port of tools/pack_builder/window.py without Build). Dear
// ImGui and plain file calls only, no game or engine calls, so the style preview tool (tools/builder_preview) draws exactly what
// the game draws. KB mods/menus-revamped/evidence/in-game-pack-builder-plan.md.
#include <filesystem>

namespace builder::shell
{
	// Before the first Draw: the OBSE plugins folder (CustomColours.ini is MenusRevamped/CustomColours.ini in it, the preset in
	// MenusRevamped.ini, the Pack Builder app's log in MenusRevamped/PackBuilder/PackBuilder.log).
	void SetPluginFolder(const std::filesystem::path& a_folder);

	// Before the renderer backend starts: the window font (a Windows font, Georgia; ImGui's own font when it is missing).
	void LoadFont();

	// Colours, rounding and sizes scaled to the game's back-buffer height (re-applied after a resize).
	void ApplyStyle(float a_viewportHeight);

	// Draws the window. false = the player closed it (title-bar X or the Close button).
	bool Draw();

	// Preview tool only: the tab shown (0 Colours, 1 Text, 2 Rows, 3 Runtime) and the Advanced switch.
	void SelectForPreview(int a_tab, bool a_advanced);
}
