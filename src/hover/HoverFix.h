#pragma once

// ============================================================================================================
// Two fixes for the game's own Inventory, Magic and barter/container menus, and a way to measure them. Game thread.
//
// 1. Hover work. Every hover the game receives makes it rebuild the details panel (Inventory about 1,100 engine objects,
//    Magic about 700; rows are not rebuilt, measured 2026-11-06). [Hover] uSettleMs (default 100): a sweep delivers
//    nothing, the row the pointer rests on is delivered once it has settled, the first hover after a rest at once, a
//    repeat of the shown item never. 0 uses [Hover] uIntervalMs instead (at most one event per interval, the last
//    held one delivered afterwards). Key and button events in Inventory/Magic deliver a held hover first: quick keys
//    and the details action button act on the last delivered hover.
// 2. The object guard: whatever creates the objects, in any menu and in gameplay, independent of the hover switches, the live count is checked once a second and a garbage collection is asked for
//    above [Guard] uLimit (at most once per cooldown).
//
// [Metrics] bEnabled=1 logs (at info level, so uLogLevel=2) every 5 s the frames per second and frame times of the open menu, the mouse
// moves, the hover events let through and skipped, the row tiles built and the objects created.
// ============================================================================================================

#include "engine/Menus.h"

namespace hoverfix
{
	void Tick(menus::Menu a_menu);   // every tick of the plugin: finds the menus' classes once they are open, delivers a held hover event, runs the guard
}
