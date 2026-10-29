#pragma once

// ============================================================================================================
// Menu Search: a search bar, typing for it and a list filter for the Inventory and Magic menus (Oblivion Remastered).
// Game thread unless said otherwise.
// ============================================================================================================

#include "engine/Menus.h"

namespace search
{
	void Tick(menus::Menu a_menu);   // once per frame (the controller read, and a timer on the game window); does the work

	void SetWindow(HWND window);
	bool HandleMessage(HWND, UINT, WPARAM, LPARAM, LRESULT&);
	void SetGameThread(DWORD a_id);   // the thread the engine's objects are used on (the one the plugin was loaded on)

	// ---- what the controller keyboard and the exports in core/Plugin.cpp use; any thread ----
	bool        Enabled(menus::Menu a_menu);   // the search is on for this menu
	std::string GetText(menus::Menu a_menu);
	void        SetText(menus::Menu a_menu, const std::string& a_text);
	bool        PanelOnLeft(menus::Menu a_menu);   // the container / barter side's list is on the left half of the screen (the keyboard goes on the other half)
	menus::Menu Context();   // the open menu for the keyboard: kContainerYours stands for the container / barter menu (both sides); kNone elsewhere
	bool        CursorOverBar();   // the mouse is over a bar that is shown (a click there is for the bar; the Improved Wheels Plus plugin leaves it alone)
	bool        Typing();   // keys are being typed into one of the bars right now (the Improved Wheels Plus plugin leaves its digit keys alone then)
	void        SetExternalInput(bool a_open);   // the controller keyboard has the text: typed keys are not taken
}
