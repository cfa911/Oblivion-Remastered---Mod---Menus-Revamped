"""RMI-038: on opening, the Alchemy mask (strip), sort icons, tabs, footer hints and the search bar appeared late or in the
wrong place, because every layout waited for the RMI-021 data gate (list built or 500 ms). The visual frame reads no item
data, so it is drawn at once; only the data reads (counts, sort, row decorations, selected filter names) still wait.
"""
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


tick = source_paths.function(src, "\tvoid Tick()")
gate = tick.find("if (!g_state.CanRead(")
check(gate > 0, "Tick still has the RMI-021 data gate")
opened = tick.find("std::exchange(g_earlyLayout, false)")
# before: the not-yet-built branch of the gate (frame only); after: everything once the list is built
before, after = (tick[gate:opened], tick[opened:]) if 0 < gate < opened else ("", tick)
check("return;" in before, "the not-yet-built branch returns before any data read")
check("Layout(menu, false)" in before, "frame layout runs before the data gate, without selected filter names")
check("LayoutFilterTabs(menu)" in before and "LayoutFooter(menu" in before, "tabs and footer hints are placed before the gate")
for data in ("ComputeCounts(", "ApplySort(", "DecorateIngredientRows(", "DecorateRows("):
    check(data not in before and data in after, f"{data} still waits for the gate")
check("g_framePlaced" in before, "the early layout retries every tick until the native widgets are measured")
check("std::exchange(g_earlyLayout, false)" in after, "the full layout runs at once when the gate opens")

layout = source_paths.function(src, "void Layout(UE::UObject* a_menu")
check("bool a_readNames" in layout and "a_readNames ? SelectedNames(a_menu)" in layout,
      "Layout can skip the view-model filter names before the list is built")
# Probe 2026-11-05 (log 21:55): on reopening the menu slides in from the left over about 220 ms; a 100 ms cadence left
# the mask strip behind. While the measured title position still changes, lay out again on the next tick.
check("g_frameMoving" in layout and "g_frameX" in layout, "Layout notes whether the native title row moved since last time")
check(before.count("g_frameMoving") >= 1 and after.count("g_frameMoving") >= 1, "both layout paths re-place every tick while the menu slides")
print(f"{checks} checks, {failures} failures")
sys.exit(1 if failures else 0)
