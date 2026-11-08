"""Static contract for the category sort on the filters (owner 2026-11-07, KB mods/menus-revamped/evidence/
own-sorts-20261107.md "Brainstorm: sort by our filters").

Mouse: left click on a filter = category sort (again = reverse), right click opens the popup. Controller: A opens the
popup, Up/Down on the filter = category sort ascending/descending. Only one sort at a time (kept on the sort host per
tab / Magic page); shown like an active sort on the filter (parchment + arrow). Default from [Picker] bCategorySort.
Rows get their group at refill time: items from the classification cache, Magic from the spell lists directly; when
the classification lands for rows that had none, the list is republished once.
"""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

picker = source_paths.read('picker')
own = source_paths.read('own-sort')
config = source_paths.read('config-core')
gear = source_paths.read('gear-data')
defaults = (source_paths.ROOT / 'config/MenusRevamped.defaults.ini').read_text(encoding='utf-8')
checks = failures = 0


def check(ok, name):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print('FAIL', name)


message = source_paths.function(picker, 'bool Picker::Message(')
check(message and 'WM_RBUTTONDOWN' in message, 'right click is handled')
check(message and 'itemsort::GroupCommand(' in message, 'left click on the filter = category sort')
rclick = message.split('WM_RBUTTONDOWN', 1)[1] if message and 'WM_RBUTTONDOWN' in message else ''
check('headerPx.Hit(' in rclick and 'pickergrid::kToggleCommand' in rclick, 'right click on the filter opens the popup')
check(message and 'WM_RBUTTONUP' in message, 'the right button release is swallowed too')

controller = source_paths.function(picker, 'void Controller(')
tail = controller.split('Only A opens the filter popup', 1)[1] if controller and 'Only A opens the filter popup' in controller else ''
check('itemsort::GroupCommand(itemsort::Action::Ascending)' in tail and 'itemsort::GroupCommand(itemsort::Action::Descending)' in tail,
      'controller Up/Down on the filter = category sort')

tick = source_paths.function(picker, 'void Picker::Tick(')
check(tick and 'itemsort::IsGroupCommand(cmd)' in tick and 'itemsort::ApplyGroup(' in tick, 'tick applies the category sort')
check(tick and 'SortHost()' in tick, 'the category sort is kept on the sort host (Magic: the school picker)')
check(tick and 'pendingRepublish' in tick, 'republish when the classification landed')
check(tick and 'SeedSorts()' in tick, 'per-tab defaults seeded once from the INI')
check('void Picker::SeedSorts()' in own, 'SeedSorts defined')
seed = source_paths.function(own, 'void Picker::SeedSorts()')
check(seed and 'categorySort' in seed and 'itemsort::Default(' in seed, 'seeding reads the INI switch')
check('PaintGroup()' in picker and 'groupBack' in picker and 'groupArrow' in picker, 'the filter shows an active category sort')
paint = source_paths.function(own, 'void Picker::PaintGroup()')
check(paint and 'kArrowDown' in paint and 'kArrowUp' in paint, 'arrow direction on the filter')
check('kMagicSorts' in picker or 'sorts[10]' in picker, 'Magic spell pages keep their own sort memory (not shared with the Inventory tabs)')

check('row.group' in own and 'itemsort::GroupRank(' in own, 'item refill sets each row group')
check('CachedRowTags(' in own and 'RowKey(' in own, 'item groups come from the classification cache by row key')
check('itemsort::MagicGroupRank(' in own and 'MagicRowMask(' in own, 'Magic refill computes groups from the spell lists')
check('g_waiting' in own or 'WaitForTags(' in own, 'rows without a group wait for the classification')
evaluate = source_paths.function(picker, 'const std::vector<UE::UObject*>& Evaluate(')
check(evaluate and 'TagsLanded(' in evaluate, 'classification landing can republish')

check('categorySort' in config and 'L"bCategorySort"' in config, 'INI [Picker] bCategorySort read')
check('bCategorySort=1' in defaults, 'defaults INI: category sort on')
book = gear[gear.find('object->As<RE::TESObjectBOOK>()'):]
book = book[:book.find('MiscTags(')]
check('object->As<RE::TESObjectBOOK>()' in gear and 'formEnchanting' in book, 'Misc: an enchanted book reaches MiscTags as a Scroll')
check('data.flags' not in book, 'RMI-052: the BOOK scroll display flag (notes, letters, maps) does not make a Scroll')

style = source_paths.function(picker, 'void Picker::StyleCells()')
check(style and 'AllTexture(' in style, 'popup "All" cell: the game icon for the school/type pickers (owner R14 note)')
brush = source_paths.function(picker, 'void Picker::BrushHeader(')
check(brush and 'AllTexture(' in brush, 'header and popup share one texture rule')

print(f'category_sort_wiring_tests: {checks} checks, {failures} failures')
sys.exit(1 if failures else 0)
