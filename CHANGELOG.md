# Changelog

## 1.4.1 - 2026-11-07

- Fixed a rare game crash when the category picker was put back into a menu's sort bar while the game was
  closing or replacing that bar (seen when looting a body). The rebuild now checks the bar is still alive and
  stops safely instead of crashing.

## 1.4.0 - 2026-11-06

- Alchemy: search ingredients by name or known effect, and search effects by name in the filter list.
- Alchemy: All, Positive Effects and Negative Effects filter tabs; each effect shows how many ingredients have it.
- Alchemy: sort ingredients by Count, Name, Weight, Value or Colors from a sort line under the Ingredients title
  (click again to reverse); each row shows its Weight and Value.
- Alchemy: a Selected Filters line with Clear all and one-click removal of each filter, up to three rows.
- Alchemy: each selected effect gets a colour, and ingredient rows show matching circles beside Weight
  (12 colours; `[Alchemy] uMarkerDiameter` and `sMarkerColors`).
- Alchemy controller: L3 names the potion with the controller keyboard, LB reaches Selected Filters, RB reaches the
  sort icons; X Create, Y Filters and hold Y Clear all stay native. A potion named with the controller keeps its name.
- Alchemy: the game's Clear Filters reliably clears every filter; selected filter rows keep their look while
  scrolling; mixed brews are listed under Potions.
- Controller keyboard: RT toggles Caps Lock everywhere.
- Hover: the details panel updates once the pointer rests on a row (`[Hover] uSettleMs`, default 100) instead of
  for every row swept past; quick keys and the Inventory action button still act on the row under the pointer.
- New INI options: `[Picker]` bInventory, bMagic, bContainer, bStolenFilter, bEnchantedFilter; `[Search]` uHotkey,
  bHotkeyCtrl; `[SearchKeyboard]` uRepeatDelayMs, uRepeatRateMs, uClearHoldMs; `[Controller]` uStickDeadzone.

## 1.3.1 - 2026-11-03

- Ring and Amulet filters now also find rings and amulets whose equipment slot was removed by
  "Unlimited Equipment of Amulets and Rings" (Nexus 497) or "Equip Unlimited Rings" (Nexus 943).

## 1.3.0 - 2026-11-02

- Search discovered world-map locations by name with a scrollable results list.
- Select a result with mouse, keyboard or controller to center the map without changing zoom or starting fast travel.
- Highlight the selected location with a gold ring that clears when the displayed map moves or zooms.
- Show native location-type icons and keep results synchronized with the map's active filters.
- Use the controller keyboard on the world map.
- Add default-enabled options to clear searches on returning to gameplay and when opening the controller keyboard.
- Improve enchanted/stolen filter spacing and refresh default artwork, removing unused icons.
- Add detailed configuration comments and world-map search placement settings.

## 1.2.0 - 2026-10-30

- Add enchanted-only item filtering with a dedicated icon at the far left of the sort bar.
- Show a blue background when enchanted filtering is active; combine it with search, category and stolen filters.
- Keep Inventory category filtering stable while sorting.
- Prevent Magic sorting controls flashing when switching tabs or entering Active Effects.

## 1.1.0 - 2026-10-30

- Add separate Magic type filters for Spells, Scrolls and Powers, combined with school, effect and search.
- Apply school and effect filters correctly to scrolls.
- Keep only one filter popup open at a time, preserving selected filters.
- Match custom header spacing to native buttons and dim inactive All headers.
- Separate Potions and Poisons and regroup their filters by effect.
- Include dedicated potion/poison artwork and updated Magic type icons.

## 1.0.0 - 2026-10-29

First unified Menus Revamped release candidate.

- Search Inventory, Magic, Quests, containers, and shops.
- Combine search with item categories, equipment pieces, and spell filters.
- Filter each side of containers and shops independently.
- Show only stolen items in Inventory and on the player's container/shop side.
- Enter searches with the integrated controller keyboard.
- Integrate Smooth Menus behavior and reduce repeated container/shop processing.
- Include updated category artwork and unified configuration.

Development compatibility checks passed with Improved Wheels Plus. The 1.0.0 shipped-default smoke test also passed. Final archive installation
acceptance is tracked separately in the KB.
