"""Static contract for RMI-006: container/barter pickers follow their side's native tab, like the Inventory menu.

Page bytes are on the container menu's VContainerMenuViewModel (game 1.512.105.0, KB docs/inventory/internals.md
"Container/barter native tabs"): CurrentPageInventory = player side (picker kind 3), CurrentPageContainer = other side (4).
A native tab change gives that tab's own choices, reset to All, with a top-level popup; the RMI-004 Transfer hold survives.
"""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

root = Path(__file__).resolve().parents[1]
gear = source_paths.read('picker')
checks = failures = 0


def check(ok, name):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print('FAIL', name)


check('"VContainerMenuViewModel"' in gear, 'reads the container view model by name')
page = source_paths.function(gear, 'int ContainerPage(')
check('a_kind == 3' in page and '"CurrentPageInventory"' in page and '"CurrentPageContainer"' in page,
      'player picker uses the player page, other picker the container page')
tick = source_paths.function(gear, 'void Picker::Tick(int a_kind, UE::UObject* a_menu')
container = tick[tick.find('if (a_kind >= 3) {'):tick.find('} else if (a_kind == 0) {')]
check('ContainerPage(a_kind)' in container and 'itemtags::TabFromPage(' in container,
      'a container side takes its tab from its native page (Misc tab -> Misc choices)')
check('Tab::AllItems' in container and container.find('Tab::AllItems') < container.find('TabFromPage('),
      'an unreadable page keeps the full All Items choices')
change = tick[tick.find('if (!tabSet || tab != wanted)'):]
change = change[:change.find('tabSet = true;')]
check('const auto hold = transfer; Reset(); transfer = hold;' in change, 'a tab change resets the picker but keeps the RMI-004 Transfer hold')
reset = source_paths.function(gear, 'void Picker::Reset()')
check('view = tab' in reset and 'styled = false' in reset,
      'Reset rebuilds the popup for the top level (owner 2026-10-27: stale Weapon type popup after a tab change)')
print(f'{checks} checks, {failures} failures')
sys.exit(1 if failures else 0)
