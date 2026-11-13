"""RMI-093: row enchanted/stolen icons not under their header toggles (barter, and Inventory rows with both icons).

Cause (probe TEMP-DIAG MRROW, KB probing/snippets/mr-row-status-icons.md, DLL 0CA97784): the offset of each visible-icon mask
was measured once, in the first layout pass, for the rows on screen then; a mask first seen later kept offset 0 (native place),
and a row's mask was read only when the game bound it. Fix: every layout pass re-places the displayed rows from their
current visibility, measures a mask the first time it is seen once the row has settled, and sets a translation only when it
changed."""
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
internal = (ROOT / "src/sorting/PickerInternal.h").read_text(encoding="utf-8-sig")
place = source_paths.function(headers, "void PlaceRowStatus(")
layout_fn = source_paths.function(headers, "bool Picker::LayoutInventoryHeaders(")
hide_fn = source_paths.function(headers, "void Picker::HideInventoryHeaderLayout(")

check("inventoryStatusMeasured" in internal, "the picker remembers which masks have a measured offset")
check("!picker.inventoryStatusMeasured[mask][i]" in place and "initialTargets && !known" not in place,
      "a mask is measured the first time it is seen, not only for icons never moved before")
check("picker.inventoryStatusMeasured[mask][i]=true" in place, "a measured mask is marked")
check("inventoryStatusMeasured={}" in hide_fn, "leaving the layout forgets the measurements (a new menu measures again)")
check("PlaceRowStatus(*this,entry,settled || inventoryHeaderReady?&plan.filters:nullptr)" in layout_fn,
      "every layout pass re-places the displayed rows, measuring only once the row has settled")
check("for (auto* entry : displayed) {\n\t\t\t\tPlaceRowStatus(*this,entry,&plan.filters);" not in layout_fn,
      "the one-time first-pass placement is gone")
check("RenderTransform" in place and "std::abs(" in place, "a translation is set only when it changed")
check("bounds.x-current" in place and place.index("current=t[0]") < place.index("bounds.x-current"),
      "the native place is the painted box minus the current translation (probe MRROW2: the box includes the translation, so a later measurement wrote the icons back to native)")
bound = source_paths.function(headers, "void OnInventoryRowBound(")
check("picker.kind!=0 || IsInventoryStatusItem(item)" in bound,
      "container/barter rows are placed when bound too (the Inventory-only item filter made them wait for the layout pass: owner \"updating on scroll\")")
check("container::SideContent(p.kind)" in bound, "a container row belongs to the picker whose side content holds it")
check("TEMP-DIAG MRROW" not in headers, "the MRROW probe is gone (result recorded in the KB)")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
