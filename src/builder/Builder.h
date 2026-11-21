#pragma once
// In-game Pack Builder (Phase 0: antivirus gate and overlay smoke test). KB mods/menus-revamped/evidence/in-game-pack-builder-plan.md,
// technique techniques/in-game-imgui-overlay.md. F11 opens and closes a Dear ImGui window drawn over the game's DX12 frames.
#include <Xinput.h>   // types only

namespace builder
{
	// At plugin load, before the renderer creates its swap chain: MinHook on dxgi.dll's CreateDXGIFactory exports.
	void Install();

	bool IsOpen();

	// First in Menus Revamped's window procedure (game window thread). true = handled, return a_result.
	bool HandleWindowMessage(HWND a_window, UINT a_message, WPARAM a_wparam, LPARAM a_lparam, LRESULT& a_result);

	// In Menus Revamped's XInputGetState chain (game thread, user 0). true = the game must read this blanked pad and
	// nothing else in Menus Revamped may act on it (the window is open, or waits for every button to be up after closing).
	bool FilterPad(XINPUT_STATE& a_state);
}
