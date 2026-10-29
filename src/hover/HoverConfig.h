#pragma once

// Compiled-in performance settings, read once from MenusRevamped.ini.

namespace smoothcfg
{
	struct Values
	{
		bool inventoryHover = true;   // [Hover] bInventory: the Inventory list's hover events are throttled
		bool containerHover = true;   // [Hover] bContainer: both barter/container lists
		bool magicHover = true;       // [Hover] bMagic: the same in the Magic menu
		int  hoverIntervalMs = 200;   // [Hover] uIntervalMs: at most one hover event per this many ms goes through (0 = none is held back)

		bool objectGuard = true;           // [Guard] bEnabled: ask the engine for a garbage collection before it runs out of object slots (the game dies at 2,162,688)
		int  objectGuardLimit = 1500000;   // [Guard] uLimit: live objects above which the guard asks for a collection
		int  objectGuardCooldown = 10;     // [Guard] uCooldown: seconds between two such requests

		bool metrics = false;   // [Metrics] bEnabled: log fps, frame times, hover events, rows rebuilt and objects created every 5 s (info level, uLogLevel=2)
	};

	Values ReadIni(const std::filesystem::path& a_ini);
}
