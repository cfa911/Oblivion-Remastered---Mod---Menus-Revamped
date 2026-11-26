#pragma once
// In-game Pack Builder window contents (Phase 2: the builder UI, a port of tools/pack_builder/window.py without Build). Dear
// ImGui and plain file calls only, no game or engine calls, so the style preview tool (tools/builder_preview) draws exactly what
// the game draws. KB mods/menus-revamped/evidence/in-game-pack-builder-plan.md.
#include <filesystem>
#include <functional>
#include <string>

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

	// Phase 4a: the live preview, wired by the overlay to builder::livepreview (the preview tool may set a status only).
	// colours: the colour file text after every edit and load; status: a line under the pack status ("" = none).
	struct LiveHooks
	{
		std::function<void(const std::string&)> colours;
		std::function<std::string()>            status;
		std::function<bool()>                   statusError;
	};
	void SetLiveHooks(LiveHooks a_hooks);

	// Preview tool only: the tab shown (0 Colours, 1 Text, 2 Rows, 3 Runtime) and the Advanced switch.
	void SelectForPreview(int a_tab, bool a_advanced);
}
