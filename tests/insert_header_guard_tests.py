"""RMI-043: the native filter-bar rebuild must not crash on a widget the game is destroying.

Crash 2026-11-07 (v1.4.0): Picker::InsertHeader cleared the native ScrollBox, then re-added a native child with an
unguarded ProcessEvent; AddChild called through a freed object's vtable. The rebuild must (1) refuse to clear the bar
unless the panel, the navigation owner and every collected child are usable (not Garbage/Unreachable/PendingKill, not
BeginDestroyed/FinishDestroyed), and (2) run every native call of the rebuild through the SEH-guarded path.
"""
import re
import sys
import source_paths

failures = []
def check(ok, label):
    if not ok:
        failures.append(label)

picker = source_paths.read('picker')
attach = source_paths.function(picker, 'bool Picker::InsertHeader(')
clear_at = attach.find('L"ClearChildren"')
check(clear_at > 0, 'InsertHeader still rebuilds through ClearChildren')
before_clear = attach[:clear_at]
check('reflect::Usable(a_panel)' in before_clear, 'panel is checked usable before the bar is cleared')
check('reflect::Usable(navigation)' in before_clear, 'navigation owner is checked usable before the bar is cleared')
check('reflect::Usable(widget)' in before_clear, 'every collected native child is checked usable before clearing')
check(re.search(r'\.Run\(\)', attach) is None, 'no unguarded ui::Call::Run in the rebuild')
check('ui::CallFirst(' not in attach, 'slot setters use the guarded CallFirst')
check('CallFirstGuarded(' in attach, 'slot setters are guarded')
check('.RunGuarded()' in attach[clear_at:], 'ClearChildren/AddChild run guarded')

reflect = (source_paths.ROOT / 'src/engine/Reflect.cpp').read_text(encoding='utf-8')
usable = source_paths.function(reflect, 'bool Usable(')
check(bool(usable), 'reflect::Usable exists')
for flag in ('Garbage', 'Unreachable', 'PendingKill', 'BeginDestroyed', 'FinishDestroyed'):
    check(flag in usable, f'Usable rejects {flag}')
check('IsLive(' in usable, 'Usable only reads flags after the slot proves the object is live')

uicall = (source_paths.ROOT / 'src/engine/UiCall.cpp').read_text(encoding='utf-8')
guarded = source_paths.function(uicall, 'bool CallFirstGuarded(')
impl = source_paths.function(uicall, 'bool CallFirstImpl(')
check('GuardedProcessEvent(' in guarded or ('CallFirstImpl(' in guarded and 'true)' in guarded
      and 'if (a_guarded) return GuardedProcessEvent(' in impl), 'CallFirstGuarded runs through the SEH guard')

for failure in failures:
    print('FAIL', failure)
print(f'15 checks, {len(failures)} failures')
sys.exit(bool(failures))
