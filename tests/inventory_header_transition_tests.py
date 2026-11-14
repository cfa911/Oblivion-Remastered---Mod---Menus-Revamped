"""Check that Inventory cosmetics use the row-binding and tab-change entry points."""
from pathlib import Path
import source_paths

root = Path(__file__).resolve().parents[1] / 'src' / 'sorting'
layout = (root / 'InventoryHeaders.cpp').read_text()
tick = (root / 'PickerTick.cpp').read_text()
sort = (root / 'OwnSort.cpp').read_text()
header = (root / 'PickerHeader.cpp').read_text()
tab = source_paths.function(tick, 'void Picker::ShowTab(')
layout_fn = source_paths.function(layout, 'bool Picker::LayoutInventoryHeaders(')
picker_src = (root / 'Picker.cpp').read_text()
checks = {
    'tab switch hides old fills before changing selection or layout':
        'SetInventoryFillOpacity(0.0f)' in tab.split('Reset()')[0],
    'RMI-077 a measured tab keeps its fills through the switch':
        'inventoryTabMeasures.Exact(' in tab.split('SetInventoryFillOpacity(0.0f)')[0],
    'RMI-077 the header layout starts from the stored tab measurement':
        'inventoryTabMeasures.For(' in layout_fn and
        layout_fn.index('inventoryTabMeasures.For(') < layout_fn.index('inventoryheaders::Arrange('),
    'RMI-077 each settled measurement is stored for its tab': 'inventoryTabMeasures.Store(' in layout_fn,
    'RMI-077 list guides hold their height while rows rebind': 'inventoryheaders::RowsBottom(' in layout_fn,
    'RMI-077 a rebuilt menu forgets tab measurements': 'inventoryTabMeasures.Clear()' in
        source_paths.function(layout, 'void Picker::ClearInventoryHeaderLayout('),
    'full-cell fills reveal after placement on the settled measured pass':
        'CanRevealControlFills(measureColumns, settled)' in layout_fn and
        layout_fn.index('placeFill(stolenFrame') < layout_fn.index('CanRevealControlFills('),
    'pending fill layout retries without disabling ready header input':
        'if (HeaderLayout() && inventoryFillPending) nextLayout=now+16;' in tick,
    'row binding applies status icon positions': 'OnListItemObjectSet' in layout and 'pe::WatchPost' in layout,
    'scroll refresh does not reposition icons based on cached viewport geometry': 'visibleOrbs' not in layout,
    'tab change places the header during the existing refresh': 'LayoutInventoryHeaders(' in tab,
    'Inventory reserves the native widths of unavailable sort controls': 'hiddenSortVisibility' in sort,
    'RMI-077 later slots follow the game resizing the A-Z wrapper':
        'inventoryheaders::SlotShifts(' in layout and 'void Picker::CompensateInventorySlots(' in layout,
    'RMI-077 the Inventory post-event compensates before the paint':
        'CompensateInventorySlots()' in source_paths.function(picker_src, 'void RefreshInventoryHeaders('),
    'RMI-078 the controller Y glyph is placed at the header row end': 'inventoryheaders::HintBox(' in layout_fn,
    'RMI-078 a cleared header gives the Y glyph back its native place':
        'inventoryHint' in source_paths.function(layout, 'void Picker::ClearInventoryHeaderLayout('),
    'RMI-077 a rebuilt header forgets the assumed slot widths':
        'inventorySlotWidths.fill(' in source_paths.function(layout, 'void Picker::HideInventoryHeaderLayout('),
    'Inventory uses the native sort background instead of drawing an underline': '6,37,32,2' not in layout,
    'row placement uses the two verified native widget names': all(
        name in layout for name in ('inv_status_enchanted', 'inv_status_stolen')),
    'tab event and tick share a layout update per frame': 'inventoryHeaderFrame' in layout,
    'new Inventory controls begin transparent': 'if (inventoryColumns || HeaderLayout() || kind == 2 || kind == 5) ui::Float(widget, L"SetRenderOpacity", 0.0f)' in header,
    'first reveal waits for settled coordinates': 'if (settled && !inventoryHeaderReady)' in layout,
    'first open reserves the header before sampling row columns when the pack lacks the inset': 'if (!bakedInset && !inventoryListReserved)' in
        source_paths.function(layout, 'bool Picker::LayoutInventoryHeaders(').split('FirstRow(list,')[0],
    'column cache requires measured item names': 'measuredName && columns' in layout,
}
for name, ok in checks.items():
    if not ok:
        print('FAIL', name)
print(f'{len(checks)} checks, {sum(not ok for ok in checks.values())} failures')
raise SystemExit(not all(checks.values()))
