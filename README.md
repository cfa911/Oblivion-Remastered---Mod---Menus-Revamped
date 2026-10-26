# Sorting Overhaul (development source)

Independent OBSE64 plugin for Inventory category/item pickers and Magic School/Effect pickers. Requires OBSE64 and the Address Library; uses the game UI through reflection. No SearchLight, MagicLoader, MISS, or third-party mod runtime dependency. Development version 0.0.0; not a release.

Build just this plugin with `xmake -y SortingOverhaul`. Install `SortingOverhaul.dll` beside the other OBSE plugins and copy `sortingoverhaul/assets/SortingOverhaul/` beside it. Log: `SortingOverhaul.log`; warnings/errors only (level 3). The plugin has its own input driver, window-message chain, menu detection, native item resolver and popup canvas lookup.

## Editable icons

All current picker/header icons load from `OBSE/Plugins/SortingOverhaul/*.png`. Replace the matching PNG and restart the game. Keep filenames unchanged. Transparent square RGBA PNGs are recommended; supplied defaults are 128 by 128 and scale to the existing icon cells. Prefixes distinguish Weapon, Gear, Alchemy, Potion, Misc, Category, School and Effect. The selected choice's PNG also appears in the header. Missing/unreadable files log one warning and do not use a hidden game-asset fallback.

Alchemy filenames: All Items **Alchemy** uses `Category-Alchemy.png`;
Alchemy root **All** uses `Alchemy-All.png`; the **Potions** parent uses
`Alchemy-Potions.png`; submenu **All potions** and its selected header use
`Potion-All.png` (singular). Back up custom art before updating. Older installs
need the new `Alchemy-All.png` and `Category-Alchemy.png` names; do not replace
custom art with procedural defaults.

This public source contains original procedural icon defaults. The owner's customized, game-derived School PNGs are kept only in the local installation, not redistributed here. Generate the eight missing procedural School placeholders before a separate installation; do not overwrite the owner's installed images. The remaining icons are original procedural defaults. `tools/generate_sorting_icons.py OUTPUT_DIRECTORY` recreates missing procedural placeholders with Pillow (it does not recreate the customized native school artwork); existing files are preserved unless `--replace` is explicitly supplied. The deployed folder is for owner customization and must never be overwritten by a later dev deploy without checking for changes.

## Optional SearchLight integration

The paired development SearchLight build exports `MenuSearch_DrivesSorting` and optionally calls `SortingOverhaul_FilterList`. Sorting Overhaul then owns the combined Inventory/Magic list filter while SearchLight supplies its exact text matcher. Without SearchLight, Sorting Overhaul drives its own list filter with no search text. Without Sorting Overhaul, SearchLight uses its ordinary filter. Older SearchLight releases have no coordination API; simultaneous filtering with those builds is not validated.

Magic picker visibility follows `VMagicMenuViewModel.GetCurrentPage`: 0 Self, 1 Touch, 2 Target, 3 All, 4 Active effects. Both headers and popups deactivate on page 4, including their mouse hit regions. Empty or powers-only spell results do not decide visibility.

## Pending validation

The item/spell-name font and eight short School labels were owner-confirmed in a development build; standalone installation, Active effects and coexistence still need separate testing. Native-sort flash SL-006, crowded popup labels, selected-detail synchronization and other video-review issues remain open. Research GearProbe/GearNativeProbe sources are retained for reference but excluded from both production targets; no diagnostic is deployed. The previous probe build launcher is retired pending a standalone research target.


## Standalone source build

Clone with `--recurse-submodules` (CommonLibOB64 is pinned), then run `xmake f -y -p windows -a x64 -m releasedbg` and `xmake -y SortingOverhaul` in a Visual Studio x64 developer prompt. DLL: `build/windows/x64/releasedbg/SortingOverhaul.dll`. The default source icons are procedural; `python tools/generate_sorting_icons.py sortingoverhaul/assets/SortingOverhaul` fills missing placeholders without replacing existing files. This source commit is not a release package; do not deploy it over customized live PNGs.

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

`SortingOverhaul.ini` beside the DLL has `[Controller] bReturnToListAfterFilter=1`.
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
