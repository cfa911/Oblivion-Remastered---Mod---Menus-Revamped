"""RMI-100 (owner 2026-11-13: "the tabs on the inventory drawn on top an the parchmentt is behind it, in the container its the
oposite"): the stock container (take) side draws its panel over its tab row. `cont_take_overlay` (export 288) Slots =
[OverlaySlot_5 cont_take_tabs (307), OverlaySlot_4 cont_take_background (306), OverlaySlot_6 list (308)], while the give side
is [panel, tabs, list]. Fix (Custom pack, bp_colour:WBP_ModernMenu_Container): swap the first two int32 entries at .uexp
112631 / 112635 (stock-checked), so the take side draws [panel, tabs, list] like the give side and the Inventory.
Facts: KB docs/inventory/internals.md "Container tab row vs panel draw order" (game 1.512.105.0)."""
import json
import struct
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
sys.path.insert(0, str(ROOT / "tools"))
from pack_builder import pipeline as pipe  # noqa: E402

PACKAGE = "WBP_ModernMenu_Container"
data = json.loads((ROOT / "tools/menus/inventory.json").read_text(encoding="utf-8"))
rows = {r["offset"]: r for r in data.get("blueprint_literals", {}).get(PACKAGE, [])}
for offset, stock, value in ((112631, 307, 306), (112635, 306, 307)):
    row = rows.get(offset, {})
    check(row.get("type") == "int32" and row.get("stock") == stock and row.get("value") == value,
          f"{PACKAGE} literal at {offset}: int32 {stock} -> {value} ({row or 'missing'})")

literals = pipe.BLUEPRINT_LITERALS.get(PACKAGE, [])
check(any(r.get("offset") == 112631 for r in literals), f"the pipeline loads the {PACKAGE} slot-order literals ({len(literals)})")

# a synthetic .uexp: the colour literals' stock values in place, the overlay slots in stock order
uexp = bytearray(120000)
for offset, expected, _ in pipe.BLUEPRINT_COLOURS.get(PACKAGE, []):
    struct.pack_into("<4f", uexp, offset, *expected)
for row in literals:
    fmt = {"double": "<d", "int32": "<i"}.get(row.get("type"), "<f")
    struct.pack_into(fmt, uexp, row["offset"], row["stock"])
VALUES = {name: [128.0, 128.0, 128.0] for name in ("pattern", "rowText", "headerText", "ornament", "white")}
try:
    out = pipe.bp_colour(bytes(uexp), PACKAGE, VALUES)
    check(struct.unpack_from("<2i", out, 112631) == (306, 307), f"the take overlay draws panel then tabs ({struct.unpack_from('<2i', out, 112631)})")
    check(len(out) == len(uexp), "the patch keeps the size")
except Exception as error:   # noqa: BLE001
    check(False, f"bp_colour patches {PACKAGE} ({error!r})")

moved = bytearray(uexp)
struct.pack_into("<i", moved, 112631, 999)
try:
    pipe.bp_colour(bytes(moved), PACKAGE, VALUES)
    check(False, "an unexpected stock slot index (game update) refuses the patch")
except pipe.SkipAsset:
    check(True, "")
except Exception as error:   # noqa: BLE001
    check(False, f"an unexpected stock slot index raises SkipAsset ({error!r})")

print(f"container_tab_order_tests: {count} checks, {len(fails)} failures")
for why in fails:
    print("  FAIL", why)
sys.exit(1 if fails else 0)
