"""Enchanted toggle lifecycle, leftmost placement and both input paths."""
import sys
from pathlib import Path
import source_paths

source = source_paths.read('picker')
failures = []
def check(ok, label):
    if not ok:
        failures.append(label)
def body(head):
    return source_paths.function(source, head)

attach = body('bool Picker::InsertHeader(')
check('L"Enchanted.png"' in attach, 'owner icon is used')
check(0 <= attach.find('addHeader(enchanted)') < attach.find('addHeader(stolen)'),
      'enchanted is attached before stolen and category controls')
check(attach.find('make(L"SlEnchantedHeader"') < attach.find('make(L"SlStolenHeader"'),
      'custom controller registration follows leftmost icon order')
check('widget == reflect::Get(enchantedHeader)' in attach, 'reattachment does not duplicate enchanted')
check('enchantedOnly' not in body('void Picker::Reset()'), 'tab reset preserves toggle')
check('enchantedOnly = false' in body('void Picker::Deactivate()'), 'menu close resets toggle')
tick = body('void Picker::Tick(')
check('enchantedOnly = !enchantedOnly' in tick, 'command toggles filter')
check('enchantedPx =' in tick and 'ui::Measure(enchanted' in tick, 'mouse bounds follow rendered button')
check('enchantedPx.Hit(x, y)' in body('bool Picker::Message('), 'mouse click queues toggle')
controller = body('void Controller(')
check('picker.EnchantedFocused()' in controller and 'enchantedfilter::kToggleCommand' in controller,
      'controller focus and confirm are supported')
check('enchantedFocus' in body('void UpdateNativeBackgroundMask()'), 'controller focus masks native background')
check((Path(__file__).resolve().parents[1]/'assets/Sorting/Enchanted.png').is_file(), 'icon is packaged')
for failure in failures:
    print('FAIL', failure)
print(f'12 checks, {len(failures)} failures')
sys.exit(bool(failures))
