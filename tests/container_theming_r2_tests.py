"""RMI-091 round 2 (owner 2026-11-13, full-size previews): Floyd-Steinberg error diffusion with blur 3 for the container panel,
the details page and the Inventory page (which gets a paper grain on its own tone instead of a flat colour), the Magic page
art (MagicPage.png, a PNG: grain, no BC encoding), and the thin lines around the container name in the lighter accent.

Facts (KB docs/inventory/internals.md "Container panel and header-line art", game 1.512.105.0): the lines are rows 60-62 and
194-196 of T_Trading_Title_Background (also Recharge, Repair, Spell Purchase, Training), tinted (0.5469, 0.4519, 0.3276) on
the take side at WBP_ModernMenu_Container .uexp 115088; the ordered 4x4 dither showed as lines and points on screen."""
import json
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

rows, menus = {}, {}
for path in (ROOT / "tools/menus").glob("*.json"):
    data = json.loads(path.read_text(encoding="utf-8"))
    menus[path.stem] = data
    for row in data.get("assets", []):
        rows[row["asset"]] = row

WANT = {
    "Art/UI/Modern/GameMenuLayer/Trading/T_Trading_Title_Background": ("band_lines:1.6", ["pattern"]),
}
for asset, (recipe, colours) in WANT.items():
    row = rows.get(asset)
    check(row is not None and row["recipe"] == recipe and row.get("colours", []) == colours,
          f"{asset}: recipe {recipe}, colours {colours} ({row and (row['recipe'], row.get('colours'))})")
check("Recharge" in rows.get("Art/UI/Modern/GameMenuLayer/Trading/T_Trading_Title_Background", {}).get("shared_with", []),
      "the name band names its other menus")

entries = {e["offset"]: e for m in menus.values() for e in m.get("blueprint_colours", {}).get("WBP_ModernMenu_Container", [])}
e = entries.get(115088, {})
check(e.get("stock") == [0.5469, 0.4519, 0.3276, 1.0] and e.get("colour") == "white" and e.get("widget") == "cont_take_info_background",
      f"the take-side name band tint is white (both sides draw the lines in the texture's own blue) ({e})")

for name, numbers, colours in (("page_grain:3:7.5", (), ("frame", "panel", "parchmentDark", "parchmentLight")), ("band_lines:1.6", (), ("pattern",))):
    try:
        got = pipe.recipe(name)[:2]
    except Exception as error:   # noqa: BLE001
        got = repr(error)
    check(got == (numbers, colours), f"recipe {name} reads {numbers} {colours} ({got})")


def alpha_halves(data):
    head = pipe.header_size(data)
    return [data[at:at + 8] for at in range(head, len(data), 16)]


try:
    from PIL import Image
    # error diffusion: a gentle ramp between two ladder greys keeps each column's average, with no repeating 4x4 tile
    flat = Image.new("F", (64, 64))
    flat.putdata([18.0 + 4.0 * x / 63 for y in range(64) for x in range(64)])
    blocks = bytes(16 * 16 * 16)
    try:
        out = pipe.neutral_grey_fs(blocks, flat)
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            probe = Image.new("RGBA", (64, 64), (0, 0, 0, 255))
            probe.save(work / "p.dds", pixel_format="DXT5")
            data = (work / "p.dds").read_bytes()
            head = pipe.header_size(data)
            (work / "fs.dds").write_bytes(data[:head] + out)
            img = pipe.decode_rgba(work / "fs.dds")
        values = [p[0] for p in img.get_flattened_data()]
        mean = sum(values) / len(values)
        tiles = {tuple(img.getpixel((bx * 4 + x, by * 4 + y))[0] for y in range(4) for x in range(4))
                 for by in range(16) for bx in range(16)}
        cols = [sum(img.getpixel((x, y))[0] for y in range(64)) / 64 - (18.0 + 4.0 * x / 63) for x in range(4, 60)]
        check(abs(mean - 20.0) < 0.6 and max(abs(c) for c in cols) < 1.5,
              f"Floyd-Steinberg keeps the average grey ({mean:.2f} for 20; worst column {max(abs(c) for c in cols):.2f})")
        check(len(tiles) > 4, f"Floyd-Steinberg has no repeating 4x4 tile ({len(tiles)} distinct tiles)")
        check(all(abs(r - b) <= 1 and abs(g - r) <= 3 for r, g, b, a in img.get_flattened_data()), "Floyd-Steinberg greys are neutral")
    except Exception as error:   # noqa: BLE001
        check(False, f"neutral_grey_fs exists and runs ({error!r})")

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        # an Inventory-like page: dark frame (brightness 30) around a light paper (190..230 grain), opaque
        page = Image.new("RGBA", (64, 64))
        for y in range(64):
            for x in range(64):
                inside = 8 <= x < 56 and 8 <= y < 56
                v = (190 + (x * 7 + y * 13) % 40) if inside else 30
                page.putpixel((x, y), (v, v, v, 255))
        stock = work / "page.dds"
        page.save(stock, pixel_format="DXT5")
        try:
            out = pipe.recipe("page_grain:3:7.5")[2](stock, {"frame": [16.0] * 3, "panel": [41.0] * 3, "parchmentDark": [8.0] * 3, "parchmentLight": [33.0] * 3}, work, None)
            (work / "page_out.dds").write_bytes(out)
            img = pipe.decode_rgba(work / "page_out.dds")
            paper = [img.getpixel((x, y))[0] for y in range(16, 48) for x in range(16, 48)]
            frame = [img.getpixel((x, y))[0] for y in range(0, 4) for x in range(0, 64)]
            check(alpha_halves(out) == alpha_halves(stock.read_bytes()), "page_grain keeps every alpha half")
            check(max(paper) - min(paper) >= 4 and abs(sum(paper) / len(paper) - 30.75) <= 4,
                  f"page_grain: the paper has a grain around the shared tone 30.75 ({min(paper)}..{max(paper)})")
            check(max(frame) <= 16 + 4, f"page_grain keeps the frame at the frame tone ({max(frame)})")
        except Exception as error:   # noqa: BLE001
            check(False, f"page_grain runs ({error!r})")
        # the name band: black shadow + two thin warm lines -> the lines in pattern x 1.6, the shadow stays black
        band = Image.new("RGBA", (64, 32), (0, 0, 0, 150))
        for x in range(64):
            for y in (8, 24):
                band.putpixel((x, y), (137, 122, 106, 255))
        bp = work / "band.dds"
        band.save(bp, pixel_format="DXT5")
        try:
            out = pipe.recipe("band_lines:1.6")[2](bp, {"pattern": [66.0, 99.0, 153.0]}, work, None)
            (work / "band_out.dds").write_bytes(out)
            img = pipe.decode_rgba(work / "band_out.dds")
            r, g, b, a = img.getpixel((32, 8))
            check(abs(r - 106) <= 12 and abs(g - 158) <= 12 and abs(b - 245) <= 12, f"band_lines: the lines are (106, 158, 245) ({(r, g, b)})")
            check(max(img.getpixel((32, 16))[:3]) <= 6, f"band_lines: the shadow stays black ({img.getpixel((32, 16))})")
            check(alpha_halves(out) == alpha_halves(bp.read_bytes()), "band_lines keeps every alpha half")
        except Exception as error:   # noqa: BLE001
            check(False, f"band_lines runs ({error!r})")
except ImportError as error:
    check(False, f"Pillow needed ({error})")

# the Magic page art (MagicPage.png) is gone: the page is baked (round 3, paper_invert_tests)

print(f"container_theming_r2_tests: {count} checks, {len(fails)} failures")
for why in fails:
    print("FAIL:", why)
sys.exit(1 if fails else 0)
