#pragma once
// In-game Pack Builder window contents (Phase 1: the empty tabbed shell). Pure Dear ImGui, no game or engine calls, so the
// style preview tool (tools/builder_preview) draws exactly what the game draws. KB mods/menus-revamped/evidence/in-game-pack-builder-plan.md.

namespace builder::shell
{
	// Before the renderer backend starts: the window font (a Windows font, Georgia; ImGui's own font when it is missing).
	void LoadFont();

	// Colours, rounding and sizes scaled to the game's back-buffer height (re-applied after a resize).
	void ApplyStyle(float a_viewportHeight);

	// Draws the window. false = the player closed it (title-bar X or the Close button).
	bool Draw();
}
