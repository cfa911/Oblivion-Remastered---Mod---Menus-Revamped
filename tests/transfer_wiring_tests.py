"""Static contract for the RMI-004 Transfer hold wiring (the timing itself is in transfer_hold_tests.cpp).

The container's native Transfer action takes our container-side header out before the native body runs, and the
layout pass does not put it back while the hold is active. Only the container side (picker 4) is affected.
"""
from pathlib import Path
import re, sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

root = Path(__file__).resolve().parents[1]
container = source_paths.read('container')
gear = source_paths.read('picker')
checks = failures = 0


def check(ok, name):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print('FAIL', name)


on_event = container[container.find('void OnEvent('):container.find('void Tick()')]
check('transferhold::IsTransferAction(name)' in on_event and 'g_transferStarted()' in on_event, 'container watcher signals the Transfer action')
check('transferhold::IsSettled(name)' in on_event and 'g_transferSettled()' in on_event, 'container watcher signals the settled sequence')
check('pe::Watch(cls, &OnEvent)' in container, 'signals come from the pre-body watcher')

check(re.search(r'void OnTransferStarted\(\)\s*\{\s*g_pickers\[3\]\.DetachForTransfer\(\);', gear) is not None, 'only the container-side picker is taken out')
check(re.search(r'void OnTransferSettled\(\)\s*\{\s*g_pickers\[3\]\.transfer\.Settle\(', gear) is not None, 'settle goes to the container-side picker')
check('container::SetTransferHandlers(&OnTransferStarted, &OnTransferSettled)' in gear, 'handlers are registered')

detach = source_paths.function(gear, 'void Picker::DetachForTransfer()')
check('kind != 4' in detach, 'detach is limited to the container side')
check('RemoveFromParent' in detach and 'transfer.Start(' in detach, 'detach removes the header and starts the hold')
check('transfer.Held()' in detach, 'a repeated action while held does nothing')
gate_at = detach.find('transferhold::ShouldDetach(NativeNameShown(')
check(gate_at >= 0 and gate_at < detach.find('RemoveFromParent'), 'only an unplaced native A-Z (quick loot) takes the header out')
check('SideContent(3)' not in detach, 'the give side visibility is not the test (controller details view hides it)')

layout = gear[gear.find("if (!attached && transfer.Held()"):]
gate = layout.find('transfer.MayReinsert(now, NativeNameShown(panel))')
insert = layout.find('InsertHeader(panel, iconBox)')
check(gate >= 0 and insert > gate, 'reinsertion is gated by the hold before InsertHeader')
check('transfer = {};' in source_paths.function(gear, 'void Picker::Reset()'), 'Reset clears the hold')

print(f'{checks} checks, {failures} failures')
sys.exit(1 if failures else 0)
