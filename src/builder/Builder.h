#pragma once
// In-game Pack Builder (Phase 1: the overlay). KB mods/menus-revamped/evidence/in-game-pack-builder-plan.md,
// technique techniques/in-game-imgui-overlay.md. A Dear ImGui window drawn over the game's DX12 frames; keyboard and mouse
// only (owner, 2026-11-21): opened by F11 or a click on the gear button in the player menu; closed by F11, Escape or Close.
#include <Xinput.h>   // types only

namespace builder
{
	// At plugin load, before the renderer creates its swap chain: MinHook on dxgi.dll's CreateDXGIFactory exports.
	void Install();

	bool IsOpen();

	// Any thread: open (true) or close (false) the window; applied on the game window's thread.
	void RequestOpen(bool a_open, const char* a_why);

	// Game thread, from core::Tick once reflection works: closes the window when the game state under it changes and
	// keeps the gear button shown with the player menu.
	void Tick();

	// First in Menus Revamped's window procedure (game window thread). true = handled, return a_result.
	bool HandleWindowMessage(HWND a_window, UINT a_message, WPARAM a_wparam, LPARAM a_lparam, LRESULT& a_result);

	// In Menus Revamped's XInputGetState chain (game thread, user 0). true = the game must read this blanked pad and
	// nothing else in Menus Revamped may act on it (the window is open, or waits for every button to be up after closing).
	bool FilterPad(XINPUT_STATE& a_state);

	namespace gear
	{
		void Tick();                                   // game thread, from builder::Tick
		bool Hit(HWND a_window, LPARAM a_position);    // window thread: a client point on the shown gear
		void Hover(bool a_over);                       // window thread: the mouse is over it (amber art)
	}
}
