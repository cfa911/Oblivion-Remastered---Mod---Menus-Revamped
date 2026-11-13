"""RMI-091 container theming round (owner 2026-11-13, "Done, go ahead"; previews approved): the container panel and the Inventory
details page with a smoother grain (blur 1.5, grain 60%, dithered neutral greys), the Inventory preview box re-encoded neutral
(blur 1.5 + dither: no green rings), and the lines around the container name in the lighter accent blue (pattern x 1.6).

Facts (KB docs/inventory/internals.md "Container panel and header-line art", game 1.512.105.0): panel T_UI_MenuLeft (BC3
512x512, also Alchemy filters, Recharge, Repair, Sigil Stone, Training, Help); lines T_Line_glow / T_Line_glow_long (BC3,
Container, Recharge, Repair) and T_InventorySeparator tinted (0.162, 0.1095, 0.0782) by WBP_ModernMenu_Container at .uexp
115047 (cont_take_details_line) and 115376 (inv_item_details_line); details page T_SpellPurchase_LeftPageBG; preview box
T_InventoryPreview_Background_D."""
import json
import statistics
import struct
import sys
import tempfile
from pathlib import Path

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
from pack_builder.model import linear  # noqa: E402

rows, menus = {}, {}
for path in (ROOT / "tools/menus").glob("*.json"):
    data = json.loads(path.read_text(encoding="utf-8"))
    menus[path.stem] = data
    for row in data.get("assets", []):
        rows[row["asset"]] = row

WANT = {
    # T_UI_MenuLeft and T_SpellPurchase_LeftPageBG: round 3 recipes, checked in paper_invert_tests
    "Art/UI/Modern/GameMenuLayer/Inventory/T_InventoryPreview_Background_D": ("gray_smooth:1.5", [], None),
    "Art/UI/Common/T_Line_glow": ("line_tint:1.6", ["pattern"], "Repair"),
    "Art/UI/Common/T_Line_glow_long": ("line_tint:1.6", ["pattern"], "Recharge"),
}
for asset, (recipe, colours, shared) in WANT.items():
    row = rows.get(asset)
    check(row is not None and row["recipe"] == recipe and row.get("colours", []) == colours
          and (shared is None or shared in row.get("shared_with", [])),
          f"{asset}: recipe {recipe}, colours {colours}, shared with {shared} ({row and (row['recipe'], row.get('colours'))})")

# the two separator tints of the container blueprint: pattern x 1.6, stock-checked
STOCK = [0.162, 0.1095, 0.0782, 1.0]
entries = {e["offset"]: e for m in menus.values() for e in m.get("blueprint_colours", {}).get("WBP_ModernMenu_Container", [])}
for offset, widget in ((115047, "cont_take_details_line"), (115376, "inv_item_details_line")):
    e = entries.get(offset, {})
    check(e.get("stock") == STOCK and e.get("colour") == "pattern" and e.get("scale") == 1.6 and e.get("widget") == widget,
          f"WBP_ModernMenu_Container {offset} ({widget}): pattern x 1.6 on the stock tint ({e})")

# recipes: the colour-file elements each reads
for name, numbers, colours in (("smooth_grain:1.5:0.6", (), ("parchmentDark", "parchmentLight", "panel")),
                               ("details_smooth:1.5:0.6", ("detailsPage",), ()),
                               ("gray_smooth:1.5", (), ()),
                               ("line_tint:1.6", (), ("pattern",))):
    try:
        got = pipe.recipe(name)[:2]
    except Exception as error:   # noqa: BLE001
        got = repr(error)
    check(got == (numbers, colours), f"recipe {name} reads {numbers} {colours} ({got})")


def blocks_alpha(data):
    head = pipe.header_size(data)
    return [data[at:at + 8] for at in range(head, len(data), 16)]


def neutral(img):
    return all(abs(r - b) <= 1 and abs(g - r) <= 3 for r, g, b, a in img.get_flattened_data() if a > 0)


try:
    from PIL import Image
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        # a noisy paper: a 2-pixel checker of two warm tones (the blocky grain) on a light-dark ramp, opaque
        paper = Image.new("RGBA", (64, 64))
        for y in range(64):
            for x in range(64):
                base = 150 + x
                v = base + (25 if (x // 2 + y // 2) % 2 else -25)
                paper.putpixel((x, y), (v, v - 8, v - 16, 255))
        stock = work / "paper.dds"
        paper.save(stock, pixel_format="DXT5")
        data = stock.read_bytes()
        try:
            out = pipe.recipe("smooth_grain:1.5:0.6")[2](stock, {"parchmentDark": [8.0] * 3, "parchmentLight": [33.0] * 3, "panel": [41.0] * 3}, work, None)
            (work / "smooth.dds").write_bytes(out)
            img = pipe.decode_rgba(work / "smooth.dds")
            old = pipe.parchment(stock, 8.0, 33.0)
            (work / "old.dds").write_bytes(old)
            before = statistics.pstdev(p[0] for p in pipe.decode_rgba(work / "old.dds").get_flattened_data())
            after = statistics.pstdev(p[0] for p in img.get_flattened_data())
            values = [p[0] for p in img.get_flattened_data()]
            check(len(out) == len(data) and out[:pipe.header_size(data)] == data[:pipe.header_size(data)],
                  "smooth_grain keeps the stock size and header")
            check(blocks_alpha(out) == blocks_alpha(data), "smooth_grain keeps every BC3 alpha half")
            check(neutral(img), "smooth_grain is neutral grey (no green or warm cast)")
            check(min(values) >= 30.75 - 7.5 - 4 and max(values) <= 30.75 + 7.5 + 4 and abs(sum(values) / len(values) - 30.75) <= 4,
                  f"smooth_grain sits on the shared paper tone 30.75 +- 7.5 (RMI-091 round 2; {min(values)}..{max(values)})")
            check(after < before * 0.6, f"smooth_grain is much smoother than the parchment bake (stdev {after:.1f} vs {before:.1f})")
        except Exception as error:   # noqa: BLE001
            check(False, f"smooth_grain runs ({error!r})")
        try:
            out = pipe.recipe("details_smooth:1.5:0.6")[2](stock, {"detailsPage": 0.01}, work, None)
            (work / "page.dds").write_bytes(out)
            img = pipe.decode_rgba(work / "page.dds")
            values = [p[0] for p in img.get_flattened_data()]
            check(blocks_alpha(out) == blocks_alpha(data) and neutral(img), "details_smooth: neutral, alpha halves kept")
            check(max(values) <= 22 and statistics.mean(values) >= 3,
                  f"details_smooth keeps the detailsPage tone (tint 0.01: about 5..19; got {min(values)}..{max(values)})")
        except Exception as error:   # noqa: BLE001
            check(False, f"details_smooth runs ({error!r})")
        # the preview box: a smooth radial glow; the Pillow-encoded gray bake bands it and tints it green
        glow = Image.new("RGBA", (64, 32))
        for y in range(32):
            for x in range(64):
                v = max(0, 90 - round(((x - 32) ** 2 + (y * 2) ** 2) ** 0.5 * 1.3))
                glow.putpixel((x, y), (v + 10 if v else 0, v + 4 if v else 0, v, 255))
        box = work / "box.dds"
        glow.save(box, pixel_format="DXT5")
        try:
            out = pipe.recipe("gray_smooth:1.5")[2](box, {}, work, None)
            (work / "box_out.dds").write_bytes(out)
            img = pipe.decode_rgba(work / "box_out.dds")
            check(blocks_alpha(out) == blocks_alpha(box.read_bytes()) and neutral(img), "gray_smooth: neutral, alpha halves kept")
            centre, edge = img.getpixel((32, 0))[0], img.getpixel((2, 31))[0]
            check(centre > 60 and edge < 20, f"gray_smooth keeps the glow ({centre} centre, {edge} corner)")
        except Exception as error:   # noqa: BLE001
            check(False, f"gray_smooth runs ({error!r})")
        # the glow lines: near-white with an alpha glow -> the lighter accent at the brightest point, alpha kept
        line = Image.new("RGBA", (64, 32), (0, 0, 0, 0))
        for x in range(4, 60):
            for y in range(12, 20):
                line.putpixel((x, y), (250, 233, 200, 140 - abs(x - 32) * 4))
        lp = work / "line.dds"
        line.save(lp, pixel_format="DXT5")
        try:
            out = pipe.recipe("line_tint:1.6")[2](lp, {"pattern": [66.0, 99.0, 153.0]}, work, None)
            (work / "line_out.dds").write_bytes(out)
            img = pipe.decode_rgba(work / "line_out.dds")
            r, g, b, a = img.getpixel((32, 16))
            check(blocks_alpha(out) == blocks_alpha(lp.read_bytes()), "line_tint keeps every BC3 alpha half")
            check(abs(r - 106) <= 10 and abs(g - 158) <= 10 and abs(b - 245) <= 10,
                  f"line_tint: the brightest point is pattern x 1.6 = (106, 158, 245) ({(r, g, b)})")
        except Exception as error:   # noqa: BLE001
            check(False, f"line_tint runs ({error!r})")
except ImportError as error:
    check(False, f"Pillow needed ({error})")

# bp_colour honours the scale: the separator tint = linear(pattern x 1.6), stock alpha kept
uexp = bytearray(115400)
for offset in (115047, 115376):
    struct.pack_into("<4f", uexp, offset, *STOCK)
struct.pack_into("<4f", uexp, 115088, 0.5469, 0.4519, 0.3276, 1.0)   # round 2: the name band tint (set to white)
try:
    out = pipe.bp_colour(bytes(uexp), "WBP_ModernMenu_Container", {"pattern": [66.0, 99.0, 153.0]})
    got = struct.unpack_from("<4f", out, 115047)
    want = [linear(min(255.0, c * 1.6)) for c in (66.0, 99.0, 153.0)]
    check(all(abs(a - b) < 2e-3 for a, b in zip(got[:3], want)) and got[3] == 1.0,
          f"the separator tint is linear(pattern x 1.6) ({got[:3]} vs {want})")
    names = pipe.recipe("bp_colour:WBP_ModernMenu_Container")[1]
    check(names == ("pattern",), f"the container blueprint bake reads pattern ({names})")
except Exception as error:   # noqa: BLE001
    check(False, f"bp_colour on WBP_ModernMenu_Container ({error!r})")

print(f"container_theming_tests: {count} checks, {len(fails)} failures")
for why in fails:
    print("FAIL:", why)
sys.exit(1 if fails else 0)
