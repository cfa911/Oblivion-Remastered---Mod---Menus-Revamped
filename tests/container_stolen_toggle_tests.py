"""RMI-094: the container side gets the stolen toggle (owner 2026-11-13: "containers can have stollen items and the sort isint
there"; scope: containers only, not merchants, whose goods are never stolen).

Before: stolenfilter::Supports(kind) was the Inventory (0) and the player side (3) only. Now kind 4 (the other side) supports
it unless the menu is a barter, recognised like the search bar's "Seller" hint: the merchant's gold row
cont_take_money_verticalbox is on screen."""
import re
import sys

import source_paths

fails = []
count = 0


def check(ok, why):
    global count
    count += 1
    if not ok:
        fails.append(why)


ROOT = source_paths.ROOT
rule = (ROOT / "src/sorting/rules/StolenFilter.h").read_text(encoding="utf-8-sig")
menu_h = (ROOT / "src/engine/ContainerMenu.h").read_text(encoding="utf-8-sig")
menu_cpp = (ROOT / "src/engine/ContainerMenu.cpp").read_text(encoding="utf-8-sig")
header = (ROOT / "src/sorting/PickerHeader.cpp").read_text(encoding="utf-8-sig")
evaluator = (ROOT / "src/sorting/CategoryEvaluator.cpp").read_text(encoding="utf-8-sig")

check("constexpr bool Supports(int kind, bool barter = false) { return kind == 0 || kind == 3 || (kind == 4 && !barter); }" in rule,
      "the stolen toggle: Inventory, player side, and the container side unless it is a barter")
check("bool IsBarter();" in menu_h, "the container menu says whether it is a barter")
barter_fn = source_paths.function(menu_cpp, "bool IsBarter(")
check('"cont_take_money_verticalbox"' in barter_fn and "ShownOnScreen" in barter_fn,
      "a barter is the menu with the merchant's gold row on screen")
check(re.search(r"stolenfilter::Supports\(kind,\s*kind == 4 && container::IsBarter\(\)\)\s*&&\s*mrconfig::Get\(\)\.picker\.stolenFilter", header)
      is not None, "the stolen toggle is made on the container side (not in a barter), still behind [Picker] bStolenFilter")
check("stolenfilter::Supports(primary.kind, primary.kind == 4 && container::IsBarter()) && (side == 1 || side == 3 || side == 4)" in evaluator,
      "the stolen-only filter applies on the container side too")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
