"""IMP-MR-006: Clear all on the Alchemy active-filter line.

Calling the menu's ClearAllFilters Enhanced Input event stub through ProcessEvent does nothing (2026-11-04 run: selected
filters 2 -> 2, three tries), so Clear all must remove every selected filter the way a single entry does.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

src = source_paths.read("alchemy")
checks = failures = 0


def check(ok, why):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print("FAIL", why)


check('std::exchange(g_nativeClearRequested, false)) RemoveEveryFilter(menu)' in src,
      "native clear events route to the same deferred action as the chip")
branch = re.search(r"if \(meaning\.empty\(\)\)(.*?)\n\t\t\t\}", src, re.S)
helper = src[src.find("void RemoveEveryFilter("):]
helper = helper[:helper.find("\n\t\t}\n")]
# the Clear all button and hold X (IMP-MR-005 controller swap) share RemoveEveryFilter
check(branch is not None and "RemoveEveryFilter(menu)" in branch.group(1) and "SelectedNames(a_menu)" in helper
      and "RemoveFilter(a_menu" in helper, "Clear all removes each selected filter")
layout = src[src.find("void Layout("):]
clear_at, names_at = layout.find('texts.push_back("Clear all")'), layout.find("for (const auto& n : names)")
check(0 <= clear_at < names_at, "Clear all is the leftmost entry (owner 2026-11-04)")
print(f"{checks} checks, {failures} failures")
sys.exit(1 if failures else 0)
