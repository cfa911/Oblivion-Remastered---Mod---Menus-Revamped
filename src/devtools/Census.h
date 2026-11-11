#pragma once
// Dev builds only (xmake f --devtools=y defines MR_DEVTOOLS; release DLLs do not contain this). Ctrl+Shift+F12 in any
// menu writes every widget of the game's UI layout that is on screen - name, class, visibility, colour, brush art, brush
// tint, text - plus the rows of each list, to Documents/My Games/Oblivion Remastered/OBSE/Logs/MenusRevamped-census-*.txt.
// Use it to map a menu before recolouring it (KB docs/reverse-engineering/dark-mode-ui.md).
#include <Windows.h>

namespace devtools
{
	bool Message(UINT a_message, WPARAM a_key, LPARAM a_position);   // window procedure: true = the census key (swallowed)
	void Tick();                                                     // game thread: writes the census once after the key
}
