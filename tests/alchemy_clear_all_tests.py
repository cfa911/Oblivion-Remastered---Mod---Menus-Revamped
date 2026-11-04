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


check("ClearAllFilters" not in src, "no call to the ClearAllFilters input-event stub (it does nothing)")
branch = re.search(r"if \(meaning\.empty\(\)\)(.*?)\n\t\t\t\}", src, re.S)
check(branch is not None and "SelectedNames(menu)" in branch.group(1) and "RemoveFilter(menu" in branch.group(1),
      "Clear all removes each selected filter")
layout = src[src.find("void Layout("):]
clear_at, names_at = layout.find('texts.push_back("Clear all")'), layout.find("for (const auto& n : names)")
check(0 <= clear_at < names_at, "Clear all is the leftmost entry (owner 2026-11-04)")
print(f"{checks} checks, {failures} failures")
sys.exit(1 if failures else 0)
