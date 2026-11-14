"""The game's header separator replaces our upper header rule on the Inventory strip (Inventory, container/barter sides),
in every preset (owner 2026-11-13: "Then replace it for our line", "it should be on vanilla as well"; choice "Native line,
full width").

Probe TEMP-DIAG MRSEP (KB probing/snippets/mr-header-separator.md, DLL 827153E1): HeaderList_Separator (2 units, full list
width) sits directly above our upper rule (sep.y = upper.y - 2) in kinds 0, 3 and 4. So the plugin no longer hides it there
and no longer draws its own upper rule; nothing is baked or changed at run time (the native line is the stock look of each
preset). Magic keeps its own separator handling (its Header_Box differs; Step C.2)."""
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
headers = (ROOT / "src/sorting/InventoryHeaders.cpp").read_text(encoding="utf-8-sig")
rules = (ROOT / "src/sorting/rules/InventoryHeaderLayout.h").read_text(encoding="utf-8-sig")
layout_fn = source_paths.function(headers, "bool Picker::LayoutInventoryHeaders(")

hides = [m.start() for m in re.finditer(r'"HeaderList_Separator"', layout_fn)]
check(len(hides) == 1 and "if (magic && measureColumns && !bakedOverflow)" in layout_fn,
      "only Magic with an older or absent layout pack collapses its separator at runtime")
check("SetRenderOpacity\",0.0f);   // TEMP-DIAG MRSEP" not in layout_fn and "TEMP-DIAG MRSEP" not in headers,
      "the MRSEP probe is gone (result recorded in the KB)")
check("SideSeparator(" not in headers, "no separator lookup is left for the Inventory strip")
check("inline bool DrawsUpperRule(int kind)" in rules and "return kind==1 || kind==6;" in rules,
      "only the Magic headers draw our own upper rule")
check("DrawsUpperRule(kind)" in layout_fn and "kGuides-2" in layout_fn,
      "the guide loop skips our upper rule where the game's separator replaces it")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
