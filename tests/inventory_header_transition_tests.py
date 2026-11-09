"""Check that Inventory cosmetics use the row-binding and tab-change entry points."""
from pathlib import Path
import source_paths

root = Path(__file__).resolve().parents[1] / 'src' / 'sorting'
layout = (root / 'InventoryHeaders.cpp').read_text()
tick = (root / 'PickerTick.cpp').read_text()
sort = (root / 'OwnSort.cpp').read_text()
header = (root / 'PickerHeader.cpp').read_text()
tab = source_paths.function(tick, 'void Picker::ShowTab(')
checks = {
    'row binding applies status icon positions': 'OnListItemObjectSet' in layout and 'pe::WatchPost' in layout,
    'scroll refresh does not reposition icons based on cached viewport geometry': 'visibleOrbs' not in layout,
    'tab change places the header during the existing refresh': 'LayoutInventoryHeaders(' in tab,
    'Inventory reserves the native widths of unavailable sort controls': 'hiddenSortVisibility' in sort,
    'Inventory uses the native sort background instead of drawing an underline': '6,37,32,2' not in layout,
    'row placement uses the two verified native widget names': all(
        name in layout for name in ('inv_status_enchanted', 'inv_status_stolen')),
    'tab event and tick share a layout update per frame': 'inventoryHeaderFrame' in layout,
    'new Inventory controls begin transparent': 'inventoryColumns) ui::Float(widget, L"SetRenderOpacity", 0.0f)' in header,
    'first reveal waits for settled coordinates': 'if (settled && !inventoryHeaderReady)' in layout,
    'first open reserves the header before sampling row columns': 'if (!inventoryListReserved)' in
        source_paths.function(layout, 'bool Picker::LayoutInventoryHeaders(').split('FirstRow(list,')[0],
    'column cache requires measured item names': 'measuredName && columns' in layout,
}
for name, ok in checks.items():
    if not ok:
        print('FAIL', name)
print(f'{len(checks)} checks, {sum(not ok for ok in checks.values())} failures')
raise SystemExit(not all(checks.values()))
