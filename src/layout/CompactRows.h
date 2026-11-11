#pragma once
// Compact rows ([Layout] bCompactRows, owner 2026-11-11): smaller list rows and icons, smaller fonts and taller list panels in
// Magic, Inventory, Alchemy, Enchanting, Spellmaking, Spell Purchase, Stats skills and Quest - the layout of FuzzUI - Interface
// Tweaks, written at run time on the game's own widgets (values and rules: layout/rules/CompactRowsTable.h).
namespace compactrows
{
	// From the 200 ms discovery block: watches each blueprint class once it has loaded (game thread).
	void Tick();
}
