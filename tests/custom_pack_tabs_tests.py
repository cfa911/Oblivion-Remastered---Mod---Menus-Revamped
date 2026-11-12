"""Custom pack builder decision 34 (owner 2026-11-12): the Inventory and Magic category tab atlases are recoloured from the
original game tabs (recipe tab_paper) to the owner's Near Black reference, instead of being drawn anew (tab:<n>, magic_tab:<n>).
Measured against the reference (KB mods/menus-revamped/evidence/custom-pack-builder-plan.md "Owner style requests during 28a"):
- alpha = stock (every pixel); greys;
- paper = tabPaperDark at stock luminance 80 .. tabPaperLight at 210 (linear in Rec. 601 luminance, extended both ways): the
  fit 9 + 0.0976 x luminance with the defaults 17 and 30;
- the glyph and arrow strokes (warm brown: (R - B) / luminance ramp from 0.24 over 0.06) inside the glyph and arrow boxes take
  tabGlyph; nothing else does;
- three new [Colours] keys; the six [Tab] greys are retained with no effect and leave the builder window."""
import json
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


root = source_paths.ROOT
sys.path.insert(0, str(root / "tools"))
import custom_colours  # noqa: E402
import pack_builder.pipeline as pipe  # noqa: E402
from pack_builder import model  # noqa: E402

from PIL import Image  # noqa: E402

KEYS = {"tabPaperDark": [17.0, 17.0, 17.0], "tabPaperLight": [30.0, 30.0, 30.0], "tabGlyph": [255.0, 255.0, 255.0]}

# ---- 1. keys ----
dark = custom_colours.from_darkstyle((root / "src/core/DarkStyle.h").read_text(encoding="utf-8"))
reader = (root / "src/core/rules/CustomColours.h").read_text(encoding="utf-8")
for key, value in KEYS.items():
    check(custom_colours.COLOURS.get(key) == value, f"{key} default = {value} ({custom_colours.COLOURS.get(key)})")
    check(custom_colours.HELP.get(key, "").startswith("Inventory and Magic category tab"), f"{key} is labelled")
    check(dark.get(key) == value, f"DarkStyle.h holds {key}")
    check(f"MR_CC_RGB({key}," in reader, f"the plugin reader knows {key}")
    check(any(key in r["members"] and "Category tabs" in r["label"] for r in custom_colours.ROLES.values()) or
          (key == "tabGlyph" and key in custom_colours.ROLES["icons"]["members"]), f"{key} is fed by a category tab (or icon) role")
rendered = custom_colours.render(custom_colours.DEFAULTS)
check("[Tab] entries are retained with no effect" in rendered and "[Tab] entries are retained with no effect" in reader,
      "the colour file says the [Tab] greys have no effect (Python and plugin default files agree)")
check((root / "config/CustomColours.ini").read_text(encoding="utf-8") == rendered, "config/CustomColours.ini is the rendered default file")
with tempfile.TemporaryDirectory() as temp:
    ini = Path(temp) / "c.ini"
    ini.write_text(rendered, encoding="utf-8")
    pages = model.ColourModel(ini).pages()
    check(not any(label.startswith("Tabs") for label, _ in pages), f"the builder window has no [Tab] page ({[l for l, _ in pages]})")
    check(not any(e.section == "Tab" for _, groups in pages for _, entries in groups for e in entries), "no [Tab] entry in the window")

# ---- 2. data rows ----
rows = {}
for path in sorted((root / "tools/menus").glob("*.json")):
    for row in json.loads(path.read_text(encoding="utf-8")).get("assets", []):
        rows[row["asset"]] = (path.stem, row)
INV, MAG = "Art/UI/Modern/GameMenuLayer/Inventory/Tabs/", "Art/UI/Modern/GameMenuLayer/magic/Tabs/"
for asset, menu in [(INV + n, "inventory") for n in ("T_Tab_weapons", "T_Tab_armor", "T_Tab_alchemy", "T_Tab_misc", "T_Tab_all")] + \
                   [(MAG + n, "magic") for n in ("T_Magic_Tabs_Self", "T_Magic_Tabs_Touch", "T_Magic_Tabs_Target", "T_Magic_Tabs_All",
                                                  "T_Magic_Tabs_ActiveEffect")]:
    got_menu, row = rows.get(asset, (None, {}))
    check(got_menu == menu and row.get("recipe") == "tab_paper", f"{Path(asset).name}: {menu}.json tab_paper ({got_menu}, {row.get('recipe')})")
    check(sorted(row.get("colours", [])) == sorted(KEYS), f"{Path(asset).name}: reads the three tab keys ({row.get('colours')})")
try:
    floats, rgbs, _ = pipe.recipe("tab_paper")
    check(sorted({*floats, *rgbs}) == sorted(KEYS), f"tab_paper reads exactly the three keys ({floats}, {rgbs})")
except Exception as error:   # noqa: BLE001
    check(False, f"pipeline.recipe('tab_paper') exists ({type(error).__name__}: {error})")

# ---- 3. the recipe on a synthetic atlas ----
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    W, H = 1024, 128
    atlas = Image.new("RGBA", (W, H))
    px = atlas.load()
    for y in range(H):
        for x in range(W):
            px[x, y] = (200, 190, 175, 255 if y < 100 else 255 - 2 * (y - 100))   # parchment (luminance ~191), fading alpha
    for y in range(30, 70):   # a brown glyph stroke in the first glyph box (centre 173) and one outside every box
        for x in range(160, 170):
            px[x, y] = (110, 60, 35, 255)
        for x in range(1000, 1010):
            px[x, y] = (110, 60, 35, 255)
    for y in range(45, 55):   # an arrow mark at 108
        for x in range(104, 112):
            px[x, y] = (120, 70, 45, 255)
    path = temp / "tab.dds"
    atlas.save(path, pixel_format="DXT5")
    stock = Image.open(path).convert("RGBA")
    # the clean category masks the recipe reads (stock(name) -> path): mask 0 covers the first glyph's stroke, the rest empty
    for i, name in enumerate(pipe.TAB_MASKS):
        mask = Image.new("RGBA", (256, 256))
        if i == 0:
            mask.paste((255, 255, 255, 255), (40, 40, 216, 216))
        mask.save(temp / f"{name}.png")
    masks = lambda name: temp / f"{name}.png"   # noqa: E731
    try:
        check(pipe.recipe("tab_paper")[2] is not None, "tab_paper has a function")
        try:
            pipe.recipe("tab_paper")[2](path, dict(KEYS), temp, None)
            check(False, "tab_paper refuses to run without the clean masks")
        except pipe.SkipAsset:
            check(True, "tab_paper refuses to run without the clean masks")
        out_bytes = pipe.recipe("tab_paper")[2](path, dict(KEYS), temp, masks)
        check(len(out_bytes) == path.stat().st_size, "tab_paper: size kept")
        (temp / "o.dds").write_bytes(out_bytes)
        out = Image.open(temp / "o.dds").convert("RGBA")
        check(all(out.getpixel((x, y))[3] == stock.getpixel((x, y))[3] for y in range(0, H, 3) for x in range(0, W, 7)), "tab_paper: alpha = stock")
        paper = out.getpixel((600, 20))
        lum = 0.299 * 200 + 0.587 * 190 + 0.114 * 175
        want = 17 + (30 - 17) * (lum - 80) / 130
        check(all(abs(c - want) <= 5 for c in paper[:3]), f"tab_paper: parchment -> the paper line ({paper[:3]} vs {want:.1f}; BC3 red/blue step 8)")
        glyph, arrow, stray = out.getpixel((165, 50)), out.getpixel((108, 50)), out.getpixel((1005, 50))
        check(min(glyph[:3]) >= 230, f"tab_paper: a glyph stroke takes tabGlyph ({glyph[:3]})")
        check(min(arrow[:3]) >= 230, f"tab_paper: an arrow mark takes tabGlyph ({arrow[:3]})")
        check(max(stray[:3]) <= 30, f"tab_paper: a brown stroke outside the glyph and arrow boxes stays paper ({stray[:3]})")
        out2 = pipe.recipe("tab_paper")[2](path, dict(KEYS, tabGlyph=[200.0, 40.0, 40.0], tabPaperDark=[0.0, 0.0, 60.0],
                                                      tabPaperLight=[0.0, 0.0, 60.0]), temp, masks)
        (temp / "o2.dds").write_bytes(out2)
        o2 = Image.open(temp / "o2.dds").convert("RGBA")
        g2, p2 = o2.getpixel((165, 50)), o2.getpixel((600, 20))
        check(g2[0] > g2[1] + 100, f"tab_paper: tabGlyph colours the glyphs ({g2[:3]})")
        check(p2[2] > p2[0] + 10, f"tab_paper: tabPaperDark/Light colour the paper ({p2[:3]})")
        # RMI-088 (owner 2026-11-12): a paper grey between BC3's red/blue levels (step 8.2; green has step 4) decoded green,
        # (24, 28, 24) for a wanted 29 (PIL truncates to 565). Stock luminance 200 -> 29 with the defaults: the decoded paper must stay neutral and
        # keep its brightness on average (the gradient is not banded to the red/blue levels).
        flat = Image.new("RGBA", (W, H))   # parchment grey 200 with a slight texture (+-4), like the stock band
        flat.putdata([(v, v, v, 255) for y in range(H) for x in range(W) for v in [200 + 2 * ((x * 7 + y * 3) % 5 - 2)]])
        flat_path = temp / "flat.dds"
        flat.save(flat_path, pixel_format="DXT5")
        (temp / "o3.dds").write_bytes(pipe.recipe("tab_paper")[2](flat_path, dict(KEYS), temp, masks))
        with Image.open(temp / "o3.dds") as im3:
            o3 = im3.convert("RGBA")
        area = [o3.getpixel((x, y))[:3] for y in range(96, 128) for x in range(300, 700)]
        worst = max(max(p) - min(p) for p in area)
        mean = [sum(p[i] for p in area) / len(area) for i in range(3)]
        check(worst <= 2, f"RMI-088: the paper decodes neutral (largest channel spread {worst}, mean {[round(m, 1) for m in mean]})")
        check(all(abs(m - 29.0) <= 1.5 for m in mean), f"RMI-088: the paper keeps its grey on average ({[round(m, 1) for m in mean]} vs 29)")
    except Exception as error:   # noqa: BLE001
        check(False, f"tab_paper works ({type(error).__name__}: {error})")

print(f"{count} checks, {len(fails)} failures")
for why in fails:
    print("FAIL:", why)
sys.exit(1 if fails else 0)
