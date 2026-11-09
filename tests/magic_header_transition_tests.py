"""Magic revamp (owner 2026-11-09): the Magic menu uses the Inventory's single header row, its flash guards and a
search bar in the title row. Source checks for the wiring; the geometry is in magic_header_layout_tests.cpp."""
import re
from pathlib import Path
import source_paths

repo = Path(__file__).resolve().parents[1]
root = repo / 'src' / 'sorting'
layout = (root / 'InventoryHeaders.cpp').read_text()
tick = (root / 'PickerTick.cpp').read_text()
internal = (root / 'PickerInternal.h').read_text()
ini = (repo / 'config' / 'MenusRevamped.defaults.ini').read_text()
search_cfg = (repo / 'src' / 'search' / 'SearchConfig.cpp').read_text()
layout_fn = source_paths.function(layout, 'bool Picker::LayoutInventoryHeaders(')
hide_fn = source_paths.function(layout, 'void Picker::HideInventoryHeaderLayout(')
clear_fn = source_paths.function(layout, 'void Picker::ClearInventoryHeaderLayout(')
opacity_fn = source_paths.function(layout, 'void Picker::SetInventoryHeaderOpacity(')
fill_fn = source_paths.function(layout, 'void Picker::SetInventoryFillOpacity(')
deactivate_fn = source_paths.function(tick, 'void Picker::Deactivate(')
tab_fn = source_paths.function(tick, 'void Picker::ShowTab(')
tick_fn = source_paths.function(tick, 'void Picker::Tick(')


def section(text, name):
    m = re.search(r'^\[' + re.escape(name) + r'\]\s*$(.*?)(?=^\[|\Z)', text, re.S | re.M)
    return m.group(1) if m else ''


magic_ini = section(ini, 'Magic')
checks = {
    'one predicate names the menus with the single header row (Inventory, Magic spell pages, Active Effects)':
        re.search(r'bool HeaderLayout\(\) const \{ return kind == 0 \|\| kind == 1 \|\| kind == 6; \}', internal) is not None,
    'the header layout runs for Magic, not only for the Inventory':
        'if (!HeaderLayout() || !OwnSorts()) return false;' in layout_fn,
    'Magic rows come from the spell list': 'Magic_Spell_List' in layout,
    'Magic reserves the strip on the spell list retainer slot': 'SpellListRetainerBox' in layout,
    'Magic measures the name and cost/magnitude columns':
        'Magic_entry_name' in layout_fn and 'Magic_entry_CostMagnitude' in layout_fn,
    'Magic row columns are read from the Magic row box': 'Magic_entry_horizontal' in layout,
    'the spell pages place the Effect and Type pickers with the School host':
        'g_pickers[1]' in layout and 'g_pickers[4]' in layout,
    'Magic hides the native rule under the title row like the Inventory':
        'HeaderList_Separator' in layout_fn and 'kind==0' not in layout_fn.split('HeaderList_Separator')[0][-200:],
    'the Magic controller Y glyph comes from the Magic filter row': 'Magic_Filters' in layout_fn,
    'the peers fade with the host until the layout settles': 'Filters(' in opacity_fn and 'Filters(' in fill_fn,
    'a cleared Magic header gives the peers their native place': 'Filters(' in clear_fn,
    'leaving a Magic page hides the header and gives back the list space':
        'if (HeaderLayout()) HideInventoryHeaderLayout();' in deactivate_fn,
    'a Magic page change keeps a measured page\'s fills and places the header in the same frame':
        'HeaderLayout() && !inventoryTabMeasures.Exact(' in tab_fn and 'HeaderLayout() && reflect::Get(header)' in tab_fn,
    'the per-frame layout pass places the Magic header': 'if (HeaderLayout()) { LayoutInventoryHeaders(' in tick_fn,
    'an unsettled Magic header takes no clicks': 'if (HeaderLayout() && !inventoryHeaderReady)' in tick_fn,
    'pending Magic fills retry at frame rate': 'if (HeaderLayout() && inventoryFillPending) nextLayout=now+16;' in tick_fn,
    'the Magic search bar sits in the title row (right of the page title)':
        re.search(r'^uX\s*=\s*440\b', magic_ini, re.M) is not None and re.search(r'^uY\s*=\s*258\b', magic_ini, re.M) is not None,
    'the compiled Magic bar default matches the ini': 'place(L"Magic", { 440, 258, 460, 36 }' in search_cfg,
    'the Quest bar keeps its own place': re.search(r'^uX\s*=\s*294\b', section(ini, 'Quest'), re.M) is not None,
    'a Quest bar without keys keeps the old place (not the moved Magic bar)':
        'v.questBar = place(L"Quest", v.magicBar' not in search_cfg,
    'RMI-081 a released strip remembers its native top for the picker that takes it over':
        'g_released' in hide_fn and 'g_released' in source_paths.function(layout, 'bool ReserveList('),
    'RMI-080 the Magic row columns are the named row boxes (probe 2026-11-09), not the cost text inside its box':
        all(f'"{n}"' in layout_fn for n in ('SpellType_SizeBox', 'School_SizeBox', 'Cost_SizeBox', 'Effect_SizeBox')),
    'RMI-079 Magic draws its own page title label over the native one (the RMI-073 pattern)':
        'MrMagicTitleLabel' in layout and 'PlaceMagicTitleLabel(' in layout_fn,
    'RMI-079 spell pages and Active Effects create their title labels and guides under their own names '
    '(a second widget with the same name in the same menu replaces the first in place)':
        'L"MrMagicTitleLabel"+std::to_wstring(picker.kind)' in layout and 'L"MrMagicHeaderGuide"+std::to_wstring(kind)' in layout_fn,
    'RMI-079 a re-created Magic label or guide never reuses the name of a removed one (creation failed; probe MRMAGIC6)':
        layout.count('g_widgetSerial++') >= 2,
    'RMI-079 a Magic title label made during a tab switch takes the native colour fully opaque (not mid-fade)':
        'colour[3]=1.0f' in source_paths.function(layout, 'void PlaceMagicTitleLabel('),
    'RMI-079 the Magic title label is placed from the title row (Header_Box), not the squeezed native title box':
        '"Header_Box"' in source_paths.function(layout, 'void PlaceMagicTitleLabel('),
    'RMI-079 the Magic title label follows a page change every tick, not only on the layout timer':
        'SyncMagicTitle()' in tick_fn,
    'RMI-079 a hidden Magic header hides its title label (spell pages <-> Active Effects use one label each)':
        'titleLabel' in hide_fn,
    'no Magic slot compensation: spell pages never resize a wrapper (probe 2026-11-09)':
        'if (kind!=0 || !inventoryHeaderReady) return;' in source_paths.function(layout, 'void Picker::CompensateInventorySlots('),
}
failures = [name for name, ok in checks.items() if not ok]
for name in failures:
    print('FAIL:', name)
print(f'{len(checks)} checks, {len(failures)} failures')
raise SystemExit(1 if failures else 0)
