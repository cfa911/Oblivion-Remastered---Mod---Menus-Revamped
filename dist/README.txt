Menus Revamped 1.4.1
Oblivion Remastered - OBSE64 plugin

REQUIREMENTS
OBSE64 and the Address Library matching your game. Development validation used
game version 1.512.105.0. Improved Wheels Plus is an optional companion;
use its Menus Revamped-compatible version.

INSTALL
Close the game completely. Back up your existing settings and artwork.
Disable SearchLight/Menu Search, its Controller Keyboard, Sorting Overhaul,
and standalone Smooth Menus before enabling Menus Revamped. Do not run those
DLLs alongside MenusRevamped.dll. Install this archive through Vortex and deploy.
Manual installation: merge the OblivionRemastered folder into the game root.
Launch through OBSE64.

FEATURES
Search Inventory, Magic, Quests, containers, shops and discovered world-map locations.
Combine text with item and spell categories, subtypes, enchanted and stolen filters.
Each container/shop side has independent filters. Includes a controller keyboard,
native map location icons, live map-filter matching and smoother menu navigation.
WORLD-MAP SEARCH
Click the map search bar or press Ctrl+F. Type part of a discovered location name.
Browse with Up/Down, PageUp/PageDown, mouse wheel or controller D-pad/left stick.
Click, Enter or controller A centers the map without changing zoom or fast traveling.
A gold ring marks the selection until the map moves or zooms. Escape/B dismisses.
The controller keyboard also works here. Results follow the native map filters.
Local-map search and special permanently non-travelable mod markers are not covered.
ALCHEMY
Search ingredients by name or known effect, and effects by name in the filter tabs
(All, Positive, Negative; each effect shows its ingredient count). Sort by Count, Name,
Weight, Value or Colors; click again to reverse. Selected Filters removes one filter
with a click or all with Clear all. Selected effects get colours shown as circles on
matching ingredients. Controller: L3 names the potion, LB Selected Filters, RB sort;
X, Y and hold Y stay native. RT toggles Caps Lock on the controller keyboard.
CONTROLS
Click a search bar to type. Enter/Escape leaves typing mode. Ctrl+A selects
the text; Delete removes selected text. Click picker icons to choose a filter.
Hold D-pad right for 400 ms to open the keyboard. In containers/shops,
D-pad right targets your side; D-pad left targets the other side.
The controller keyboard displays its button prompts: A types, X deletes
(hold to clear), Y inserts a space, and B finishes.

CONFIGURATION AND MIGRATION
MenusRevamped.ini is beside MenusRevamped.dll. Restart after changing settings.
[Search] bWorldMap=1 enables world-map search; [Map] sets bar position and size.
[Search] bClearTextOnGameplay=1 clears queries on returning to gameplay.
[SearchKeyboard] bClearTextOnActivation=1 clears the bar when the keyboard opens.
Set either clearing option to 0 to keep previous text. Menu-page switching retains it.
[Picker], [Search] uHotkey, [SearchKeyboard], [Controller], [Alchemy] and [Hover]
uSettleMs hold the 1.4.0 options; each is explained in the INI.
Default artwork lives in MenusRevamped/Search, Keyboard and Sorting.
Runtime reads the unified configuration only, not the old standalone INIs.
For custom legacy settings/artwork, use tools/stage_menus_revamped.py from the
source archive with --plugins OLD_PLUGINS --destination EMPTY_STAGING_FOLDER.
Inspect its output before installing it. Existing unified settings/artwork win.
Keep backups; copying old INIs under a new filename is not a supported migration.

UNINSTALL
Close the game, disable/remove the package through Vortex and redeploy.
Restore backed-up standalone packages/settings if returning to the old setup.

SUPPORT AND SOURCE
https://github.com/cfa911/Oblivion-Remastered---Mod---Menus-Revamped
Report the mod/game versions, input method, menu and reproduction steps.
GPL-3.0-or-later. See LICENSE, NOTICE.md and THIRD_PARTY_NOTICES.md.
Credit: cfa911.
