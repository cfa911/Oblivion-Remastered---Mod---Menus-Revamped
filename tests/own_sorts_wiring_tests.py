"""Static contract for the own sort bars (owner 2026-11-07, KB mods/menus-revamped/evidence/own-sorts-20261107.md).

Inventory (kind 0) and both container/barter sides (kinds 3/4): our sort buttons (game textures loaded at run time)
after the category picker; every native sort wrapper collapsed; the list order set by permuting the native refill's
`Items` struct array before the Blueprint copies it into the rows; the chosen sort kept per side and tab across tab
changes and openings; a click republishes through the view model's own sort setter.
"""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

root = Path(__file__).resolve().parents[1]
picker = source_paths.read('picker')
own = source_paths.read('own-sort')
checks = failures = 0


def check(ok, name):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print('FAIL', name)


check('#include "sorting/rules/ItemSort.h"' in picker, 'picker uses the pure sort rules')
# Container/barter: the native-called custom events (UpdateGive/TakeListView run inside the Blueprint, no ProcessEvent).
for fn in ('"UpdateListViewItems"', '"UpdateInventoryListView"', '"UpdateContainerListView"'):
    check(fn in own, f'refill hook handles {fn}')
check('pe::Watch(' in own, 'the refill hook runs before the Blueprint body (pe::Watch, not WatchPost)')
check('L"/Script/Altar.OriginalInventoryMenuItemProperties"' in own, 'reads rows through the reflected item struct')
check('"Items"' in own and 'itemsort::Order(' in own, 'permutes the Items array by itemsort::Order')
for field in ('"Price"', '"Weight"', '"WeaponDamage"', '"ArmorRating"', '"Health"', '"Name"'):
    check(field in own, f'reads {field} by reflection')
check('SetSortTypeInventory' in own and 'SetSortTypeContainer' in own, 'a click republishes through the view model')
for tex in ('T_Property_1_sorting', 'T_Property_1_Health', 'T_Property_1_Attack', 'T_Property_1_defense',
            'T_Property_1_Weight', 'T_Property_1_MoneyV2', 'T_Filter_Icon_Background_Small'):
    check(tex in picker or tex in own or tex in source_paths.read('own-sort-rules'), f'game texture {tex}')

tick = source_paths.function(picker, 'void Picker::Tick(')
check(tick and 'HideNativeSorts()' in tick, 'native sort wrappers are collapsed every tick')
check(tick and 'if (justAttached) Republish(*this);' in tick, 'one republish after the bar is attached (the first refill precedes the hook)')
check('L"OnSortTypeUpdated"' in own and '"SortType"' in own, 'republish through the Blueprint path the native buttons use')
reset = source_paths.function(picker, 'void Picker::Reset()')
check(reset and 'sorts' not in reset, 'a tab change (Reset) keeps the chosen sorts')
deactivate = source_paths.function(picker, 'void Picker::Deactivate()')
check(deactivate and 'sorts' not in deactivate, 'closing the menu keeps the chosen sorts')
message = source_paths.function(picker, 'bool Picker::Message(')
check(message and 'sortPx' in message, 'mouse clicks on our sort buttons')
insert = source_paths.function(picker, 'bool Picker::InsertHeader(')
check(insert and 'sortHeaders' in insert, 'InsertHeader creates and attaches our sort buttons')

# Magic spell pages (kind 1, the school picker hosts our A-Z and Cost in the native buttons' places).
check('"UpdateCurrentPageInventory"' in own and '"NewList"' in own, 'Magic refill hook (UpdateCurrentPageInventory(NewList))')
check('L"/Script/Altar.LegacyMagicMenuItemProperties"' in own and '"Property"' in own, 'reads Magic rows: Property = cost')
check('MagicMenu_Filters_K2Node_ComponentBoundEvent_0_OnSortTypeUpdated' in own, 'Magic republish through its Blueprint sort event')
check('kind == 0 || kind == 1 || kind == 3 || kind == 4' in picker, 'own sorts on the Magic school picker too')
check(insert and 'MagicNativeKey(' in insert, 'Magic sorts take the native A-Z and Cost places')
check(insert and 'IsOwnSortHeader(widget)' not in insert, "a picker keeps the other pickers' sort buttons when it rebuilds the shared Magic bar")
check(deactivate and 'UpdateFiltersToDisplay' in deactivate, 'leaving the spell pages hands the native buttons back (Active Effects)')
check(deactivate and 'sortHeaders' in deactivate, 'our sort buttons hide when the picker deactivates')

# R8 (owner R7 test): re-insert refused at >12 children (ours stay in the bar), stale click areas, all-at-once.
check(insert and 'n > 12' not in insert and 'natives > 12' in insert and 'IsOwnedHeader' in insert.split('natives > 12')[0], 'the 12-child guard counts only the game children (no picker widgets, shared Magic bar)')
detach = source_paths.function(picker, 'void Picker::DetachForTransfer()')
check(detach and 'ClearOwnFromBar()' in detach, 'the Transfer detach takes all our widgets out (they return together)')
check('void Picker::ClearOwnFromBar()' in picker and 'enchantedHeader' in source_paths.function(picker, 'void Picker::ClearOwnFromBar()')
      and 'sortHeaders' in source_paths.function(picker, 'void Picker::ClearOwnFromBar()'), 'ClearOwnFromBar removes toggles, header and sorts')
check(tick and tick.count('ClearHitAreas()') >= 2, 'no stale click areas while the bar is not attached')
check('void Picker::ClearHitAreas()' in picker and 'sortPx' in source_paths.function(picker, 'void Picker::ClearHitAreas()'), 'ClearHitAreas clears our sort rectangles')
check(reset and 'ClearHitAreas()' in reset, 'Reset clears the click areas too')
check('UpdateFilterFocus' in picker and 'quickLootY' in picker, 'quick loot: Y opens our sort row when the game did nothing')

# Owner 2026-11-07: in our filters, D-pad/stick Down (or Up) must not act like A.
pad = source_paths.function(picker, 'void Controller(')
header_branch = pad[pad.find('// Native methods retain their original A/up/down reversal.'):] if pad else ''
header_branch = header_branch[:header_branch.find('UpdateNativeBackgroundMask();')]
updown = [line for line in header_branch.split('NavigateTo')[0].splitlines() if 'Direction::Up' in line or 'Direction::Down' in line]
check(header_branch and updown and all('GroupCommand' in line and 'kToggleCommand' not in line for line in updown),
      'only A opens a filter popup; Up/Down are the category sort (owner 2026-11-07)')

# RMI-048: with own sorts the native A-Z is always collapsed, so the RMI-004 detach (keyed on it) must not run.
check(detach and 'if (OwnSorts()) return;' in detach.split('ClearOwnFromBar()')[0], 'own sorts: the RMI-004 Transfer detach no longer runs (our A-Z replaces the native one)')

# Owner 2026-11-07: quick loot shows the Y glyph on a controller (the game hides it there; Transfer keeps the game's).
check(tick and 'ShowQuickLootHint()' in tick, 'the quick-loot Y glyph is kept in the per-frame pass')
check('"WBP_ModernPrefab_ControllerInputHint"' in own and 'FindByClassPrefix(' in own and 'PadInput()' in own and 'SideShown(3)' in own,
      'Y glyph: the filter ControllerInputHint found by class (its name is not Filter_InputHint_Horizontalbox in containers, R10 log)')

# Magic Active Effects (owner 2026-11-07): kind 6 on a free picker slot, page 4 only, sorts without a category picker.
magic_tick = (root / 'src/sorting/Picker.cpp').read_text(encoding='utf-8-sig')
check('g_pickers[2].Tick(6, a_menu' in magic_tick, 'Magic ticks the Active Effects sort host (kind 6) in a free slot')
check('kind == 0 || kind == 1 || kind == 3 || kind == 4 || kind == 6' in picker, 'kind 6 owns sorts')
check(tick and 'a_kind == 6 ? ' in tick, 'kind 6 shows on Active Effects only')
check(insert and 'ActiveKeys()' in insert and 'ActiveNativeKey(' in insert, 'kind 6 creates the five Active Effects sorts and collapses the native five')
check('ActivePicker(6)' in own and 'effectValue' in own and 'magnitude' in own, 'Active Effects refill ordered by the kind 6 host')
check(deactivate and 'kind == 1 || kind == 6' in deactivate, 'leaving Active Effects hands the native buttons back too')
check('if (kind == 6)' in source_paths.function(own, 'void Picker::ShowOwnWidgets()'), 'kind 6 shows no category picker header')

# R12 owner: Active Effects works on mouse, not on controller. Log: no focus line for kind 6: SortCarouselFocused required
# the (hidden) category header on screen.
carousel = source_paths.function(picker, 'bool Picker::SortCarouselFocused()')
check(carousel and 'kind == 6' in carousel.split('IsFilterFocused')[0], 'kind 6 controller focus does not depend on its hidden header')

# Owner 2026-11-07 (visual batch): spell tabs' type (star) and school (scroll) pickers show the game's original icons
# in their "All" state, loaded at run time (no game art shipped); the other choices keep their PNGs.
brush = source_paths.function(picker, 'void Picker::BrushHeader(')
all_texture = source_paths.function(own, 'const wchar_t* AllTexture(')
check(brush and 'AllTexture(kind)' in brush and 'TextureBrush(' in brush and all_texture and 'T_Property_1_LesserPower' in all_texture and
      'T_Property_1_school' in all_texture, 'BrushHeader: game star/scroll for All')
check(reset and 'BrushHeader(' in reset and 'Brush(image' not in reset, 'tab reset paints through BrushHeader')
check(tick and 'BrushHeader(HeaderIcon())' in tick, 'a choice paints through BrushHeader')
check(insert and 'BrushHeader(' in insert, 'the header is painted through BrushHeader when created')

print(f'own_sorts_wiring_tests: {checks} checks, {failures} failures')
sys.exit(1 if failures else 0)
