#pragma once

// ============================================================================================================
// Two fixes for the game's own Inventory, Magic and barter/container menus, and a way to measure them. Game thread.
//
// 1. The hover storm. The lists of the Inventory, Magic and barter/container menus fire an OnItemHovered event on about every other mouse move, and
//    every one of them makes the game rebuild all the visible rows; the rebuild itself fires a hover event for each row it builds.
//    On a long list that is up to 45,000 new engine objects per second: the cursor lags, the frame rate halves and, held long
//    enough, the game stops with "Maximum number of UObjects exceeded". At most one hover event per [Hover] uIntervalMs goes
//    through; the last one skipped is delivered once the interval is over, so the details panel never stays on the wrong row.
// 2. The object guard: whatever creates the objects, in any menu and in gameplay, independent of the hover switches, the live count is checked once a second and a garbage collection is asked for
//    above [Guard] uLimit (at most once per cooldown).
//
// [Metrics] bEnabled=1 logs (at info level, so uLogLevel=2) every 5 s the frames per second and frame times of the open menu, the mouse
// moves, the hover events let through and skipped, the row tiles built and the objects created.
// ============================================================================================================

#include "Menus.h"

namespace hoverfix
{
	void Tick(menus::Menu a_menu);   // every tick of the plugin: finds the menus' classes once they are open, delivers a held hover event, runs the guard
}
