# Sorting Overhaul (development source)

Independent OBSE64 plugin for Inventory category/item pickers and Magic School/Effect pickers. Requires OBSE64 and the Address Library; uses the game UI through reflection. No SearchLight, MagicLoader, MISS, or third-party mod runtime dependency. Development version 0.0.0; not a release.

Build just this plugin with `xmake -y SortingOverhaul`. Install `SortingOverhaul.dll` beside the other OBSE plugins and copy `sortingoverhaul/assets/SortingOverhaul/` beside it. Log: `SortingOverhaul.log`; warnings/errors only (level 3). The plugin has its own input driver, window-message chain, menu detection, native item resolver and popup canvas lookup.

## Editable icons

All 57 current picker/header icons load from `OBSE/Plugins/SortingOverhaul/*.png`. Replace the matching PNG and restart the game. Keep filenames unchanged. Transparent square RGBA PNGs are recommended; supplied defaults are 128 by 128 and scale to the existing icon cells. Prefixes distinguish Weapon, Gear, Potion, Misc, Category, School and Effect. The selected choice's PNG also appears in the header. Missing/unreadable files log one warning and do not use a hidden game-asset fallback.

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
