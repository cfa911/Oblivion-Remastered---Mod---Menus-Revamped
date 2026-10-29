Menus Revamped 1.0.0
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
Search Inventory, Magic, Quests, containers and shops; combine text with item
and spell filters; use independent container/shop-side categories; show only
stolen items on the player side; type using the controller keyboard.

CONTROLS
Click a search bar to type. Enter/Escape leaves typing mode. Ctrl+A selects
the text; Delete removes selected text. Click picker icons to choose a filter.
Hold D-pad right for 400 ms to open the keyboard. In containers/shops,
D-pad right targets your side; D-pad left targets the other side.
The controller keyboard displays its button prompts: A types, X deletes
(hold to clear), Y inserts a space, and B finishes.

CONFIGURATION AND MIGRATION
MenusRevamped.ini is beside MenusRevamped.dll. Restart after changing settings.
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
