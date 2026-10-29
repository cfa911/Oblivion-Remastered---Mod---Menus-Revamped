# Menus Revamped

Unified search, category pickers and controller keyboard for Oblivion Remastered.
Formerly Sorting Overhaul; the repository is now
`cfa911/Oblivion-Remastered---Mod---Menus-Revamped`.
The standalone Sorting baseline is preserved on `deprecated/sorting-overhaul`.

- One plugin: `MenusRevamped.dll`, one `MenusRevamped.ini` and one log.
- Shared native list ownership; text and category filtering combine internally.
- Search in Inventory, Magic, Quests and both container/barter sides.
- Existing picker appearance, controller gestures and custom artwork retained.
- Asset folders: `MenusRevamped/Search`, `Keyboard` and `Sorting`.
- Improved Wheels Plus uses three read-only `MenusRevamped_*` exports.
- Smooth Menus hover throttling and object guard are compiled in, including
  independent barter/container-side replay and container-first discovery.
  Owner-accepted in the development build (container/barter performance and
  the idle-focus flicker); see Validation status.

## Development build and cutover

Build with xmake windows/x64/releasedbg, target `MenusRevamped`.
Before deploying, run `tools/stage_menus_revamped.py --plugins <old Plugins>
--destination <staging folder>` to import settings/artwork without editing the
live install. Repeat staging never overwrites unified customization.

With the game completely closed, back up and remove the old `MenuSearch.dll`,
`MenuSearchControllerKeyboard.dll`, `SortingOverhaul.dll` and `SmoothMenus.dll`
and their PDBs.
Install the unified build and the matching Improved Wheels lookup update;
never run the old and new menu plugins together. Keep original settings/artwork
and the wheel's assignments for rollback. Runtime reads only unified paths.

## Provenance and tests

Performance options are in `[Hover]`, `[Guard]` and `[Metrics]` in the unified INI.
`[Hover] bContainer` controls both barter/container sides independently of
`bInventory`/`bMagic`. The migration imports missing Smooth settings without
replacing existing unified values; keep the old INI for rollback.

Sorting baseline: `53ca72a`; SearchLight/controller keyboard baseline: `45063dc`;
Smooth Menus baseline: `17ed7b1`.
All imported sources retain GPL-3.0-or-later notices. Credit: `cfa911`.
Focused tests cover real internal adapters, the unified filter and input driver,
source generations, one-time migration and the existing native resolver.
Hover behavior (replay, per-side ownership) is covered by `python tests/hover_integration_tests.py <output-directory>`
and `python tests/hover_side_ownership_tests.py <output-directory>`; migration by `tests/test_smooth_migration.py`
and `tests/test_stage_menus_revamped.py`.
Run every suite from an MSVC x64 developer prompt with
`python tests/run_all.py <output-directory> [<Improved Wheels Plus checkout>]`.

## Source layout

One include root, `src/`; includes are paths from it (`"engine/Ui.h"`).

| Folder | What it holds |
|---|---|
| `src/core` | Plugin entry and exports (`Plugin.cpp`), the tick and window hook (`Runtime.cpp`), the XInput import patch (`Input.cpp`), the unified INI (`Config`), logging, precompiled header |
| `src/engine` | ProcessEvent hook, reflection, UMG helpers (`UiCall`, `UiWidgets`, `UiTree`, `UiText`), text utilities, menu discovery (`Menus`, `ContainerMenu`, `QuestsMenu`) |
| `src/search` | The search bars (`SearchBar`), typing and IME (`SearchInput`), the list filter (`ListFilter`), item names, shared state (`SearchInternal.h`) |
| `src/keyboard` | The controller on-screen keyboard and its input step |
| `src/sorting` | The category pickers (`Picker*`), item-row reading, the category evaluator, the native background mask; `rules/` holds the header-only, unit-tested rules |
| `src/hover` | The hover throttle and object guard from Smooth Menus |
| `assets/` | Default pictures, in the same `Search`, `Keyboard`, `Sorting` folders as installed under `MenusRevamped/` |
| `config/` | `MenusRevamped.defaults.ini` |
| `tests/` | Host-double and rule tests; `source_paths.py` says where each tested unit lives |

## Validation status

This is a development build, not a release. The owner has accepted the integrated development build (SearchLight, SearchLight - Controller Keyboard, Sorting Overhaul and the Smooth Menus hover fix as one plugin) in the ordinary integration scenarios, including container/barter performance and the idle-focus flicker. Exhaustive input-device and lifecycle coverage is not claimed.

Open and fixed issues are tracked per bug in the knowledge-base repository (`bugs/menus-revamped`, `RMI-` series; older entries `SL-` and `SO-`). Do not rely on this README for the list of open problems. No research probe sources are part of this repository and no diagnostic is deployed.

## Historical Sorting feature reference

The documentation below predates integration. Standalone DLL names and
cross-plugin compatibility descriptions are historical, not current packaging.

# Sorting Overhaul (development source)

Independent OBSE64 plugin for Inventory and container/barter category/item pickers and Magic School/Effect pickers. Requires OBSE64 and the Address Library; uses the game UI through reflection. No SearchLight, MagicLoader, MISS, or third-party mod runtime dependency. Development version 0.0.0; not a release.

Historical standalone instructions (superseded by the unified build above): the Sorting module now ships inside `MenusRevamped.dll`; its artwork is copied to `OBSE/Plugins/MenusRevamped/Sorting/` and settings are in `MenusRevamped.ini`. The unified log is `MenusRevamped.log`; warnings/errors only (level 3). The plugin has its own input driver, window-message chain, menu detection, native item resolver and popup canvas lookup.

## Editable icons

All current picker/header icons load from `OBSE/Plugins/MenusRevamped/Sorting/*.png` (formerly `OBSE/Plugins/SortingOverhaul/`; `tools/stage_menus_revamped.py` imports the old folder). Replace the matching PNG and restart the game. Keep filenames unchanged. Transparent square RGBA PNGs are recommended; supplied defaults are 128 by 128 and scale to the existing icon cells. Prefixes distinguish Weapon, Gear, Alchemy, Potion, Misc, Category, School and Effect. The selected choice's PNG also appears in the header. Missing/unreadable files log one warning and do not use a hidden game-asset fallback.

Alchemy filenames: All Items **Alchemy** uses `Category-Alchemy.png`;
Alchemy root **All** uses `Alchemy-All.png`; the **Potions** parent uses
`Alchemy-Potions.png`; submenu **All potions** and its selected header use
`Potion-All.png` (singular). Back up custom art before updating. Older installs
need the new `Alchemy-All.png` and `Category-Alchemy.png` names; do not replace
custom art with procedural defaults.

This public source contains original procedural icon defaults. The owner's customized, game-derived School PNGs are kept only in the local installation, not redistributed here. Generate the eight missing procedural School placeholders before a separate installation; do not overwrite the owner's installed images. The remaining icons are original procedural defaults. `tools/generate_sorting_icons.py OUTPUT_DIRECTORY` recreates missing procedural placeholders with Pillow (it does not recreate the customized native school artwork); existing files are preserved unless `--replace` is explicitly supplied. The deployed folder is for owner customization and must never be overwritten by a later dev deploy without checking for changes.

## Optional SearchLight integration

The paired development SearchLight build exports `MenuSearch_DrivesSorting` and optionally calls `SortingOverhaul_FilterList`. Sorting Overhaul then owns the combined Inventory/Magic list filter while SearchLight supplies its exact text matcher. Without SearchLight, Sorting Overhaul drives its own list filter with no search text. Without Sorting Overhaul, SearchLight uses its ordinary filter. With an older SearchLight build, the validated container/barter sequence is picker first, then search: the picker leaves a filtered list, and nonempty legacy SearchLight text pauses further Sorting Overhaul list writes while SearchLight searches the remaining rows.

## Container and barter pickers

Each visible side has its own picker, filter choice and list snapshot. Both sides
offer the full category/subtype tree, independent of the native tab. Native row
indices resolve against the player's inventory or the other container/merchant,
including the barter flag; null row-form pointers do not disable filtering.
Owner-confirmed in development. Existing Inventory/Magic behavior is retained.

With older SearchLight, choose a container/barter sort/filter before typing:
the search then applies to the remaining filtered rows because nonempty legacy
text pauses this plugin's standalone container list writer. Changing the picker
while search text remains, clearing the search, changing tabs or sides, and
transfer-triggered rebuilds are not established by that test. The existing
delegated FilterList API accepts side IDs3/4 for coordinating clients. No
SearchLight source change is included.

Logs stay warnings/errors only and retain three previous runs. Temporary row
diagnostics are removed from this source. Focused native-boundary and dispatch
checks (MSVC x64 developer prompt):
`python tests/container_native_tests.py <output-directory>` and
`python tests/container_discovery_tests.py <output-directory>`.

Magic picker visibility follows `VMagicMenuViewModel.GetCurrentPage`: 0 Self, 1 Touch, 2 Target, 3 All, 4 Active effects. Both headers and popups deactivate on page 4, including their mouse hit regions. Empty or powers-only spell results do not decide visibility.

## Standalone source build

Clone with `--recurse-submodules` (CommonLibOB64 is pinned), then run `xmake f -y -p windows -a x64 -m releasedbg` and `xmake -y MenusRevamped` in a Visual Studio x64 developer prompt. DLL: `build/windows/x64/releasedbg/MenusRevamped.dll` (there is no separate `SortingOverhaul` target any more). The default source icons are procedural; `python tools/generate_sorting_icons.py assets/Sorting` fills missing placeholders without replacing existing files. This source commit is not a release package; do not deploy it over customized live PNGs.

## Expanded spell-effect picker

See [effect-family definitions and verification](docs/spell-effect-categories.md). Eighteen choices including All; new icons deliberately use Staff placeholders. Runtime validation pending.

## Native power membership follow-up

[Power row filtering](docs/power-row-filter.md) now derives type from resolved native spells rather than a UI-category allowlist. Targeted power-filter owner retest is pending; the expanded grid/controller/mouse scenario was already owner-confirmed.

## Native-style focus highlight

[Square borderless E6E4DF focus and reversible native background mask](docs/native-highlight.md) is implemented for owner testing. Existing icons/background and filter membership are unchanged.

## Detailed melee subfilters

[Detailed weapon kinds](docs/weapon-kinds.md) expands Blade/Blunt to eight melee
choices plus Other weapons, retaining Bows/Staves. Nine additional editable PNGs
are provisional defaults for the owner to replace. Existing icons are preserved.
SO-009 potion Magicka correction is retained. Arrows now classify native AMMO
as Weapons with a dedicated Arrows choice on both picker paths. The thirteen-choice
popup now has four columns and four rows; Weapon-Arrow.png is a provisional Blunt-art default.
Owner accepted the development work; this is not a release.

## Nested Alchemy and Misc Tools

[Alchemy/Misc filter design](docs/alchemy-misc.md): All/Potions/Ingredients/Tools,
with potion families nested under Potions, including Attributes. Apparatus Tools
is one combined leaf. Misc Tools also includes the base repair hammer and inventory
lights/torches. Four additional provisional icons, existing PNGs preserved.
Candidate requires owner runtime testing; no release or deployment implied.


## Count-based picker layout and controller focus

One filter uses1x1;2-4 use2 columns;5-9 use3 columns;10+ use4 columns.
Rows are ceil(filter count /columns):6 uses3x2 and9 uses3x3. Root/paper, title,
cells and hit regions resize/reflow together. Full inventory labels, centered
darker titles and the revised horizontal anchor preserve all icon files.

`MenusRevamped.ini` beside the DLL has `[Controller] bReturnToListAfterFilter=1` (formerly `SortingOverhaul.ini`; staging imports it).
By default, committing a final custom filter through the controller returns to
the item list using the native sorting toggle. Parent/submenu steps, cancellations,
mouse picks and native sort-method buttons do not trigger this automatic return.
Set0 to retain sorting-bar focus until you exit it. Restart after changing the INI.
Owner accepted the layout/controller batch and All Items Alchemy icon correction.
The final Alchemy-All/Potion-All filename follow-up is built/deployed for retest.

Focused checks: compile/run `tests/category_icon_tests.cpp`, `tests/picker_layout_tests.cpp`,
`tests/controller_policy_tests.cpp`, `tests/alchemy_navigation_tests.cpp`,
`tests/weapon_kinds_tests.cpp` and `tests/effect_categories_tests.cpp` with C++ latest;
run `python -B tests/icon_names_tests.py` for generator/default filename checks.
