"""RMI-091 round 3 (owner 2026-11-13): the four papers by luminance inversion + colour mapping, full strength ("A"), blur 3:
the standard parchment look ("this combo should be the standard for parchments").

Per pixel: brightness clamped to the paper's own 2nd..98th percentile, blurred (blur 3; non-paper pixels count as the paper
median, so the torn edge gets no bright rim), inverted (255 - v) and mapped so the paper median lands on the tone with every
value keeping its ratio to the median; neutral grey Floyd-Steinberg BC3 (neutral_grey_fs), alpha halves kept.
  container T_UI_MenuLeft            paper_invert:3    tone = paper_tone (~31)
  Inventory T_UI_PlayerMenu_Background page_invert:3   tone ~31; frame + ornament keep the panel curve (relief)
  Magic     T_MagicBackground        page_invert:3     baked now (Quest/Help/Stats too); the run-time MagicPage.png swap goes
  details   T_SpellPurchase_LeftPageBG details_invert:3 tone = the detailsPage band middle (~13); the drop shadow stays black
Facts: KB docs/inventory/internals.md "The four paper textures" (game 1.512.105.0)."""
import json
import re
import statistics
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

rows = {}
for path in (ROOT / "tools/menus").glob("*.json"):
    for row in json.loads(path.read_text(encoding="utf-8")).get("assets", []):
        rows[row["asset"]] = row

PAGE_COLOURS = ["frame", "panel", "parchmentDark", "parchmentLight"]
WANT = {
    "Art/UI/Common/Backgrounds/T_UI_MenuLeft": ("paper_invert:3:0.7", ["panel", "parchmentDark", "parchmentLight"]),
    "Art/UI/Common/Backgrounds/T_UI_PlayerMenu_Background": ("page_invert:3:0.7", PAGE_COLOURS),
    "Art/UI/Modern/GameMenuLayer/magic/T_MagicBackground": ("page_invert:3:0.7", PAGE_COLOURS),
    "Art/UI/Modern/GameMenuLayer/SpellPurchase/T_SpellPurchase_LeftPageBG": ("details_invert:3", ["detailsPage"]),
}
for asset, (recipe, colours) in WANT.items():
    row = rows.get(asset)
    check(row is not None and row["recipe"] == recipe and row.get("colours", []) == colours,
          f"{asset}: recipe {recipe}, colours {colours} ({row and (row['recipe'], row.get('colours'))})")
magic = rows.get("Art/UI/Modern/GameMenuLayer/magic/T_MagicBackground", {})
check(all(m in magic.get("shared_with", []) for m in ("Quest", "Help", "Stats")),
      f"the Magic page row names Quest, Help and Stats (shared, wanted) ({magic.get('shared_with')})")

for name, numbers, colours in (("paper_invert:3", (), ("parchmentDark", "parchmentLight", "panel")),
                               ("page_invert:3", (), ("frame", "panel", "parchmentDark", "parchmentLight")),
                               ("paper_invert:3:0.7", (), ("parchmentDark", "parchmentLight", "panel")),
                               ("page_invert:3:0.7", (), ("frame", "panel", "parchmentDark", "parchmentLight")),
                               ("details_invert:3", ("detailsPage",), ())):
    try:
        got = pipe.recipe(name)[:2]
    except Exception as error:   # noqa: BLE001
        got = repr(error)
    check(got == (numbers, colours), f"recipe {name} reads {numbers} {colours} ({got})")

VALUES = {"frame": [16.0] * 3, "panel": [41.0] * 3, "parchmentDark": [8.0] * 3, "parchmentLight": [33.0] * 3,
          "detailsPage": 0.01}


def alpha_halves(data):
    head = pipe.header_size(data)
    return [data[at:at + 8] for at in range(head, len(data), 16)]


def neutral(img):
    return all(abs(r - b) <= 1 and abs(g - r) <= 3 for r, g, b, a in img.get_flattened_data())


try:
    from PIL import Image

    def make(work, name, pixel):
        img = Image.new("RGBA", (64, 64))
        img.putdata([pixel(x, y) for y in range(64) for x in range(64)])
        img.save(work / f"{name}.dds", pixel_format="DXT5")
        return work / f"{name}.dds"

    def bake(work, recipe, stock):
        out = pipe.recipe(recipe)[2](stock, VALUES, work, None)
        (work / "out.dds").write_bytes(out)
        return out, pipe.decode_rgba(work / "out.dds")

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        # container: a paper with a darker (stained) left quarter
        paper = make(work, "paper", lambda x, y: ((170, 160, 150, 255) if x < 16 else (215, 205, 195, 255)))
        try:
            out, img = bake(work, "paper_invert:3", paper)
            left = statistics.mean(img.getpixel((x, y))[0] for y in range(8, 56) for x in range(2, 10))
            right = statistics.mean(img.getpixel((x, y))[0] for y in range(8, 56) for x in range(40, 60))
            values = [p[0] for p in img.get_flattened_data()]
            check(len(out) == len(paper.read_bytes()) and out[:128] == paper.read_bytes()[:128], "paper_invert keeps size and header")
            check(alpha_halves(out) == alpha_halves(paper.read_bytes()), "paper_invert keeps every BC3 alpha half")
            check(neutral(img), "paper_invert is neutral grey")
            check(left > right + 6, f"paper_invert inverts: the stained (darker) stock half comes out lighter ({left:.1f} vs {right:.1f})")
            check(20 <= statistics.median(values) <= 40, f"paper_invert sits around the shared tone ~31 ({statistics.median(values)})")
        except Exception as error:   # noqa: BLE001
            check(False, f"paper_invert runs ({error!r})")

        # Inventory/Magic page: a dark frame (top 16 rows, stock 30) over a paper with a crease
        page = make(work, "page", lambda x, y: (30, 30, 30, 255) if y < 16 else
                    ((175, 170, 165, 255) if 30 <= x < 34 else (210, 205, 200, 255)))
        try:
            out, img = bake(work, "page_invert:3", page)
            frame = [img.getpixel((x, y))[0] for y in range(0, 12) for x in range(64)]
            body = [img.getpixel((x, y))[0] for y in range(24, 64) for x in range(64)]
            crease = statistics.mean(img.getpixel((x, y))[0] for y in range(24, 64) for x in range(30, 34))
            flat = statistics.mean(img.getpixel((x, y))[0] for y in range(24, 64) for x in range(4, 20))
            check(alpha_halves(out) == alpha_halves(page.read_bytes()), "page_invert keeps every BC3 alpha half")
            check(neutral(img), "page_invert is neutral grey")
            check(max(frame) <= 19, f"page_invert keeps the frame on the panel curve (stock 30 -> frame 16; {min(frame)}..{max(frame)})")
            check(crease > flat + 2, f"page_invert inverts the crease into a lighter line, softened by the blur ({crease:.1f} vs {flat:.1f})")
            check(24 <= statistics.median(body) <= 38, f"page_invert paper sits on the shared tone ~31 ({statistics.median(body)})")
        except Exception as error:   # noqa: BLE001
            check(False, f"page_invert runs ({error!r})")

        # owner 2026-11-13: the three shared-tone papers 30% darker (<blur>:<scale>, scale 0.7 on the paper tone); the page
        # frame keeps its panel curve; the details page is not scaled
        try:
            _, base = bake(work, "paper_invert:3", paper)
            _, dark = bake(work, "paper_invert:3:0.7", paper)
            b, d = statistics.median(p[0] for p in base.get_flattened_data()), statistics.median(p[0] for p in dark.get_flattened_data())
            check(abs(d - 0.7 * b) <= 1.5, f"paper_invert:3:0.7 is 30% darker ({d} vs 0.7 x {b})")
            _, base = bake(work, "page_invert:3", page)
            _, dark = bake(work, "page_invert:3:0.7", page)
            body = lambda im: statistics.median(im.getpixel((x, y))[0] for y in range(24, 64) for x in range(64))   # noqa: E731
            frame = lambda im: max(im.getpixel((x, y))[0] for y in range(0, 12) for x in range(64))   # noqa: E731
            check(abs(body(dark) - 0.7 * body(base)) <= 1.5, f"page_invert:3:0.7 paper is 30% darker ({body(dark)} vs 0.7 x {body(base)})")
            check(frame(dark) == frame(base), f"page_invert:3:0.7 keeps the frame ({frame(dark)} vs {frame(base)})")
        except Exception as error:   # noqa: BLE001
            check(False, f"the 30% darker papers run ({error!r})")

        # details: paper inside a transparent black drop shadow (8-pixel border)
        det = make(work, "det", lambda x, y: (205, 200, 195, 255) if 8 <= x < 56 and 8 <= y < 56 else (0, 0, 0, 60))
        try:
            out, img = bake(work, "details_invert:3", det)
            border = [img.getpixel((x, y))[0] for y in range(64) for x in range(0, 6)]
            inside = [img.getpixel((x, y))[0] for y in range(16, 48) for x in range(16, 48)]
            check(alpha_halves(out) == alpha_halves(det.read_bytes()), "details_invert keeps every BC3 alpha half (the shadow)")
            check(max(border) <= 3, f"details_invert keeps the drop shadow black ({max(border)})")
            check(8 <= statistics.median(inside) <= 18, f"details_invert sits on the detailsPage band middle ~13 ({statistics.median(inside)})")
        except Exception as error:   # noqa: BLE001
            check(False, f"details_invert runs ({error!r})")
except ImportError:
    check(False, "Pillow is needed for the paper_invert checks")

# Magic page: baked, so the run-time MagicPage.png swap is gone (Custom = the only recolouring preset)
theme = (ROOT / "src/core/UiTheme.h").read_text(encoding="utf-8")
dark_art = theme.split("inline constexpr ArtSwap kDarkArt[]{", 1)[1].split("};", 1)[0] if "kDarkArt[]{" in theme else ""
check(bool(dark_art) and "T_MagicBackground" not in dark_art and "MagicPage.png" not in dark_art,
      "kDarkArt has no T_MagicBackground -> MagicPage.png swap (the page is baked)")
page_fn = source_paths.function(source_paths.read("picker"), "void StyleMagicPage(")
check(bool(page_fn) and "SwapArt(" not in page_fn and "MagicPage.png" not in page_fn and "IsMagicPage(" not in page_fn,
      "StyleMagicPage no longer swaps the page art")
check("InkRich(" in page_fn, "StyleMagicPage still inks the Spell Effectiveness texts")
check(not (ROOT / "assets/Skin/MagicPage.png").exists(), "assets/Skin/MagicPage.png is gone (nothing draws it)")
tab_art = (ROOT / "tools/make_dark_tab_art.py").read_text(encoding="utf-8")
check("MagicPage.png" not in re.sub(r'""".*?"""', "", tab_art, flags=re.S), "make_dark_tab_art.py no longer writes MagicPage.png")

print(f"paper_invert_tests: {count} checks, {len(fails)} failures")
for why in fails:
    print("FAIL:", why)
sys.exit(1 if fails else 0)
