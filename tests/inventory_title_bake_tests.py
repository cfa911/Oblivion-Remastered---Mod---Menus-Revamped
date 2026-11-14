"""Inventory tab title: baked like the container side names, no run-time title code (owner 2026-11-12/13: "yes"; no run-time
title labels, "this is how you get bugs"; the owner preferred the container's baked header over the Inventory's label).

The Inventory's title is the same inv_cont_title in WBP_ModernMenu_ContainerInventory_MainContent that the container sides
use (KB docs/inventory/internals.md "Container and barter widgets"), so the C.1 bake (title slot Auto, Spacer_156 Fill, Min
Desired Width 300; container_title_bake_tests) already covers it. What goes is the RMI-073 run-time code: our TextBlock label
over the native text, its SetText hook and per-tick refresh, and the run-time unclip pass (min width, no scrolling, retainer
off, title event watcher). The Magic page label (RMI-079) is a separate later step and stays."""
import source_paths

fails = []
count = 0


def check(ok, why):
    global count
    count += 1
    if not ok:
        fails.append(why)


ROOT = source_paths.ROOT
skin = (ROOT / "src/sorting/MenuSkin.cpp").read_text(encoding="utf-8-sig")
skin_h = (ROOT / "src/sorting/MenuSkin.h").read_text(encoding="utf-8-sig")
headers = (ROOT / "src/sorting/InventoryHeaders.cpp").read_text(encoding="utf-8-sig")
tick = (ROOT / "src/sorting/PickerTick.cpp").read_text(encoding="utf-8-sig")
rules = (ROOT / "src/sorting/rules/InventoryHeaderLayout.h").read_text(encoding="utf-8-sig")
layout_fn = source_paths.function(headers, "bool Picker::LayoutInventoryHeaders(")

check("PlaceTitleLabel(" not in headers and "MrInventoryTitleLabel" not in headers,
      "no Inventory title label is created at run time")
check('"inv_cont_title"' not in layout_fn, "the Inventory header layout does not touch the native title (baked like the sides)")
for name in ("StyleTitle", "UnclipTitle", "KeepTitleLabel", "OnTitleEvent", "RefreshInventoryTitle", "SetTitleLabel"):
    check(name not in skin and name not in skin_h and name not in headers and name not in tick,
          f"the run-time title pass {name} is gone")
check("g_titleLabel" not in skin and "g_title " not in skin and "g_title;" not in skin, "the skin holds no title handles")
check("TitleLabel(Box" not in rules and "TitleText(" not in rules, "the Inventory label rules are gone")
check("kTitleLabelWidth" not in rules and "PlaceMagicTitleLabel(" not in headers,
      "the Magic page label is baked too (C.2, magic_title_bake_tests)")

# the bake that replaces the label covers the Inventory's own title (shared MainContent package)
import sys  # noqa: E402

sys.path.insert(0, str(ROOT / "tools"))
from pack_builder import pipeline as pipe  # noqa: E402

row = pipe.LAYOUT.get("WBP_ModernMenu_ContainerInventory_MainContent", {})
check(any(b.get("offset") == 18872 and b.get("value") == 0 for b in row.get("bytes", [])),
      "the shared title slot is baked Auto (every preset)")
check(any(r.get("export_start") == 18901 for r in row.get("rewrites", [])), "the shared Spacer_156 takes the slack (Fill)")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
