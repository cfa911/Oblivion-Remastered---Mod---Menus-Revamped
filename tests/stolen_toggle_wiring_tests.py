"""Static contract for the stolen-only toggle (owner scope 2026-10-29).

Independent icon beside the category picker, Inventory (kind 0) and the player's container/barter side (kind 3) only.
Stays on across native tab changes, turns off when the menu closes. Mouse click or controller A toggles; left/right
keep native navigation. Icon file MenusRevamped/Sorting/Stolen-Item.png (owner's art). The Ctrl+F10 TEMP-DIAG probe
that proved StatusFlags bit 0 = stolen is gone from the build.
"""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

root = Path(__file__).resolve().parents[1]
gear = source_paths.read('picker')
policy_path = source_paths.paths('stolen-filter')[0]
policy = policy_path.read_text(encoding='utf-8-sig') if policy_path.exists() else ''
menu = source_paths.read('search')
build = (root / 'xmake.lua').read_text(encoding='utf-8-sig')
checks = failures = 0


def check(ok, name):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print('FAIL', name)


def body(text, marker, end):
    start = text.find(marker)
    return text[start:text.find(end, start)] if start >= 0 else ''


check('kStolenBit = 0x1' in policy, 'StatusFlags bit 0 is the stolen bit (KB inventory internals)')
check('kind == 0 || kind == 3' in policy, 'only the Inventory and the player side support the toggle')
check('L"Stolen-Item.png"' in gear, "uses the owner's Stolen-Item.png icon")
check('stolenfilter::Status StolenStatus(' in gear and '"StatusFlags"' in gear, 'reads the row StatusFlags by reflection')
reset = source_paths.function(gear, 'void Picker::Reset()')
check(reset and 'stolenOnly' not in reset.replace('PaintStolen', ''), 'a native tab change (Reset) keeps the toggle')
deactivate = source_paths.function(gear, 'void Picker::Deactivate()')
check('stolenOnly = false' in deactivate, 'closing the menu (Deactivate) turns the toggle off')
kind_change = body(gear, 'if (kind != a_kind)', 'active.store(true);')
check('stolenOnly = false' in kind_change, 'another menu starts with the toggle off')
message = source_paths.function(gear, 'bool Picker::Message(UINT a_message, WPARAM a_key, LPARAM a_position)')
check('stolenPx.Hit(x, y)' in message and 'stolenfilter::kToggleCommand' in message, 'a click on the icon toggles')
tick = source_paths.function(gear, 'void Picker::Tick(int a_kind, UE::UObject* a_menu')
check('cmd == stolenfilter::kToggleCommand' in tick and 'stolenOnly = !stolenOnly' in tick, 'the toggle command flips the state')
insert = source_paths.function(gear, 'bool Picker::InsertHeader(')
check('stolenfilter::Supports(kind)' in insert, 'the icon is created only where the toggle is supported')
check('reflect::Get(stolenHeader)' in insert, 'the icon is not re-added as a native child')
attach = insert[insert.find('if (schoolAt < 0)'):]
check(0 <= attach.find('Get(stolenHeader)') < attach.find('"Content", box'),
      'the stolen icon is the leftmost button, before the category picker (owner 2026-10-29)')
paint = source_paths.function(gear, 'void Picker::PaintStolen()')
check('kStolenFill' in policy and '0.708376f, 0.135633f, 0.114435f, 0.5f' in policy,
      'active background is #db675f at 50% (linear Slate colour)')
check('stolenfilter::kStolenFill' in paint and 'kInk' not in paint and '1.5f' not in paint,
      'active state has no outline')
controller = source_paths.function(gear, 'void Controller(std::uint16_t& buttons')
check('StolenFocused()' in controller and 'stolenfilter::kToggleCommand' in controller, 'controller A on the focused icon toggles')
evaluate = source_paths.function(gear, 'const std::vector<UE::UObject*>& Evaluate(')
check('primary.stolenOnly == primary.appliedStolen' in evaluate, 'a toggle change re-filters the list')
check('StolenProbe' not in menu and 'MR_STOLEN_PROBE' not in menu, 'the Ctrl+F10 probe is not called')
check('stolen_probe' not in build and 'StolenProbe' not in build, 'the probe build option is removed')
check(not (root / source_paths.GONE_PROBE).exists(), 'the probe source is removed')
print(f'{checks} checks, {failures} failures')
sys.exit(1 if failures else 0)
