# Changelog

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
