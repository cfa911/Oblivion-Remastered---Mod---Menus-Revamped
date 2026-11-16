"""Step C.4 Enchanting (owner look-through 2026-11-16; KB mods/menus-revamped/menus/Enchanting/CHECKLIST.md C4-1, C4-2):
- C4-1: the row icons are stretched. `WBP_ModernMenu_EnchantmentEntry` `Entry_Icon` (a WBP_OriginalImageTile) stores a
  50x50 FVector2D (doubles at .uexp 5348 / 5356); the compact row (36) clamps only its height, so the icon draws ~50 wide
  by ~34 high. Baked from a new Rows-tab size `enchantingIconSize` (default 32, like the other compact rows' icons).
- C4-2: the tab strip / parchment seam. The charcoal strips end in ~24 rows of light paper (light charcoal ~40) over the
  page (~22.5, the old parchment curve). Owner (2026-11-16): not a strip fade; "Regen parchment", tone B (31): the page
  T_Enchantment_Tab_Paper (Enchanting only, drawn with the strips by M_UI_Echantment_Tabs) moves to the parchment
  standard paper_invert:3 (inversion + blur 3) at the shared paper tone (~31 with the Dark defaults)."""
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
sys.path.insert(0, str(ROOT / "tools/pack_builder"))
import custom_colours as cc  # noqa: E402
from pack_builder import pipeline as pipe  # noqa: E402
import charcoal_tabs as ct  # noqa: E402
from PIL import Image  # noqa: E402

# ---- C4-1: the Enchanting row icon size ----
layout = json.loads((ROOT / "tools/menus/layout.json").read_text(encoding="utf-8"))
rows = layout["layout"].get("WBP_ModernMenu_EnchantmentEntry", {}).get("floats", [])
icon = [r for r in rows if r.get("widget") == "Entry_Icon"]
check(sorted(r.get("offset") for r in icon) == [5348, 5356], f"C4-1: Entry_Icon width and height rows at 5348/5356 ({icon})")
check(all(r.get("type") == "double" and r.get("stock") == 50.0 and r.get("size") == "enchantingIconSize" for r in icon) and icon,
      "C4-1: both rows are doubles, stock 50, from [Sizes] enchantingIconSize")
check(cc.SIZES.get("enchantingIconSize") == 32, f"C4-1: enchantingIconSize defaults to 32 ({cc.SIZES.get('enchantingIconSize')})")
enchanting_rows = [k for label, items in cc.ROW_LISTS if label == "Enchanting rows" for k, _, _ in items]
check("enchantingIconSize" in enchanting_rows, "C4-1: the Rows tab lists the Enchanting icon size next to its row height")

uexp = bytearray(6092)   # stock WBP_ModernMenu_EnchantmentEntry.uexp size (game 1.512.105.0)
struct.pack_into("<f", uexp, 4959, 54.0)
struct.pack_into("<d", uexp, 5348, 50.0)
struct.pack_into("<d", uexp, 5356, 50.0)
try:
    out = pipe.bp_float(bytes(uexp), "WBP_ModernMenu_EnchantmentEntry", dict(cc.SIZES))
    got = (struct.unpack_from("<d", out, 5348)[0], struct.unpack_from("<d", out, 5356)[0], struct.unpack_from("<f", out, 4959)[0])
    check(got == (32.0, 32.0, 36.0), f"C4-1: the baked row draws a 32x32 icon in a 36 row ({got})")
    out = pipe.bp_float(bytes(uexp), "WBP_ModernMenu_EnchantmentEntry", dict(cc.SIZES) | {"enchantingIconSize": 30.0})
    check(struct.unpack_from("<d", out, 5348)[0] == 30.0, "C4-1: the icon follows the owner's [Sizes] value")
except Exception as e:   # noqa: BLE001
    check(False, f"C4-1: bp_float on the Enchanting row failed: {e}")

# ---- C4-3: the Spellmaking row icon (owner 2026-11-16: "the rows on spell making still has the issue") ----
srows = layout["layout"].get("WBP_ModernMenu_SpellmakingEntry", {}).get("floats", [])
sicon = [r for r in srows if r.get("widget") == "Entry_Icon"]
check(sorted(r.get("offset") for r in sicon) == [4801, 4809], f"C4-3: Spellmaking Entry_Icon rows at 4801/4809 ({sicon})")
check(sicon and all(r.get("type") == "double" and r.get("stock") == 50.0 and r.get("size") == "spellmakingIconSize" for r in sicon),
      "C4-3: both rows are doubles, stock 50, from [Sizes] spellmakingIconSize")
check(cc.SIZES.get("spellmakingIconSize") == 32, f"C4-3: spellmakingIconSize defaults to 32 ({cc.SIZES.get('spellmakingIconSize')})")
check("spellmakingIconSize" in [k for label, items in cc.ROW_LISTS if label == "Spellmaking rows" for k, _, _ in items],
      "C4-3: the Rows tab lists the Spellmaking icon size")
suexp = bytearray(5540)   # stock WBP_ModernMenu_SpellmakingEntry.uexp size (game 1.512.105.0)
struct.pack_into("<f", suexp, 4621, 54.0)
struct.pack_into("<d", suexp, 4801, 50.0)
struct.pack_into("<d", suexp, 4809, 50.0)
try:
    out = pipe.bp_float(bytes(suexp), "WBP_ModernMenu_SpellmakingEntry", dict(cc.SIZES))
    got = (struct.unpack_from("<d", out, 4801)[0], struct.unpack_from("<d", out, 4809)[0])
    check(got == (32.0, 32.0), f"C4-3: the baked Spellmaking row draws a 32x32 icon ({got})")
except Exception as e:   # noqa: BLE001
    check(False, f"C4-3: bp_float on the Spellmaking row failed: {e}")

# ---- C4-2: the page regenerated with the parchment standard (owner chose "Regen parchment", tone B = 31) ----
enchanting = json.loads((ROOT / "tools/menus/enchanting.json").read_text(encoding="utf-8"))
page = next((a for a in enchanting["assets"] if a["asset"].endswith("/Tabs/T_Enchantment_Tab_Paper")), None)
check(page is not None and page["recipe"] == "paper_invert:1.5",   # C4-6 (owner 2026-11-16): page blur 3 -> 1.5, like the tabs
     
      f"C4-2: T_Enchantment_Tab_Paper uses the parchment standard (inversion + blur 3), not the old curve ({page and page['recipe']})")
check(page is not None and sorted(page["colours"]) == ["panel", "parchmentDark", "parchmentLight"],
      "C4-2: its colours are the shared paper tone's inputs (panel, parchmentDark, parchmentLight)")
check(page is not None and page.get("shared_with") == [], "C4-2: the page is Enchanting's own (no other menu draws it)")
tone = pipe.paper_tone(cc.COLOURS["parchmentDark"][0], cc.COLOURS["parchmentLight"][0], cc.COLOURS["panel"][0])
check(abs(tone - 31.0) <= 1.0, f"C4-2: with the Dark defaults the page median lands on ~31, the owner's tone B ({tone:.2f})")
check(ct.PRESETS.get("enchanting", {}).get("seam") is None, "C4-2: the tab strips are not faded (owner rejected the seam fade)")

# ---- C4-2 round 4 (owner 2026-11-16: "use the same technique for the tabs and the parchment; the tabs should be used on the
# parchment"; preview approved "Bake it"): the three strips' paper is the page's parchment standard (same clamp, blur 3,
# inversion and median of the PAGE's stock brightness, same tone), the charcoal ink mask keeps the icons and arrows light ----
import tempfile  # noqa: E402
from pathlib import Path  # noqa: E402
tabs = json.loads((ROOT / "tools/menus/tabs.json").read_text(encoding="utf-8"))
strips = [a for a in tabs["assets"] if "/enchantment/Tabs/T_Enchantment_Tab_" in a["asset"]]
check(len(strips) == 3 and all(a["recipe"] == "charcoal_paper:enchanting" for a in strips),
      f"C4-2: the three Enchanting strips use charcoal_paper:enchanting ({[a['recipe'] for a in strips]})")
check(all(sorted(a["colours"]) == ["panel", "parchmentDark", "parchmentLight"] for a in strips),
      "C4-2: the strips read the page tone's colours (panel, parchmentDark, parchmentLight)")
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    rng2 = __import__("random").Random(3)
    page_img = Image.new("RGBA", (256, 256))
    page_img.putdata([(v, v - 6, v - 16, 255) for _ in range(256 * 256) for v in [200 + rng2.randint(-6, 6)]])
    page_dds = temp / "T_Enchantment_Tab_Paper.dds"
    page_img.save(page_dds, pixel_format="DXT5")
    strip_img = Image.new("RGBA", (1024, 128))
    strip_img.putdata([(v, v - 6, v - 16, 255) for _ in range(1024 * 128) for v in [200 + rng2.randint(-6, 6)]])
    sp = strip_img.load()
    for y in range(30, 70):   # a dark brown stroke inside the Gear shield's icon box (168, 9, 239, 94)
        for x in range(200, 206):
            sp[x, y] = (70, 45, 25, 255)
    strip_dds = temp / "T_Enchantment_Tab_Gear.dds"
    strip_img.save(strip_dds, pixel_format="DXT5")
    colours = {"parchmentDark": [8.0] * 3, "parchmentLight": [33.0] * 3, "panel": [41.0] * 3}
    try:
        _, keys, bake = pipe.recipe("charcoal_paper:enchanting")
        stock = {"T_Enchantment_Tab_Paper": page_dds}
        out = Image.open(__import__("io").BytesIO(bake(strip_dds, colours, temp, lambda n: stock[n])))
        _, _, page_bake = pipe.recipe("paper_invert:3")
        page_out = Image.open(__import__("io").BytesIO(page_bake(page_dds, colours, temp, None)))
        g = sorted(out.convert("L").getpixel((x, y)) for y in range(90, 128, 3) for x in range(0, 1024, 7))
        pg = sorted(page_out.convert("L").getpixel((x, y)) for y in range(0, 256, 5) for x in range(0, 256, 5))
        check(abs(g[len(g) // 2] - pg[len(pg) // 2]) <= 2,
              f"C4-2: the same stock paper bakes to the same grey in the strip and the page ({g[len(g) // 2]} vs {pg[len(pg) // 2]})")
        check(out.convert("L").getpixel((203, 50)) >= 150, f"C4-2: the icon stroke stays light ({out.convert('L').getpixel((203, 50))})")
        check(out.getchannel("A").tobytes() == Image.open(strip_dds).getchannel("A").tobytes(), "C4-2: the strip keeps its stock alpha")
        # round 5 (owner chose V1 2026-11-16, "we lost the edges completely of the shapes"): the strip's own shading darker
        # than the page's range (tab edges, shadows) is not clamped away, and the blur is 1, so a tab edge stays a shape
        for y in range(100, 104):
            for x in range(300, 700):
                sp[x, y] = (120, 112, 100, 255)
        strip_img.save(strip_dds, pixel_format="DXT5")
        out = Image.open(__import__("io").BytesIO(bake(strip_dds, colours, temp, lambda n: stock[n]))).convert("L")
        edge = sum(out.getpixel((x, 102)) for x in range(320, 680, 8)) / 45
        paper_row = sum(out.getpixel((x, 116)) for x in range(320, 680, 8)) / 45
        check(edge - paper_row >= 12, f"C4-2 V1: a dark tab edge keeps its own line, not clipped flat ({edge:.1f} vs paper {paper_row:.1f})")
        check(pipe.CHARCOAL_PAPER_BLUR == 1.5, f"C4-2 V1 + owner \"add some bluriness ... like 1.5\": the strip blur is 1.5 ({pipe.CHARCOAL_PAPER_BLUR})")
    except Exception as e:   # noqa: BLE001
        check(False, f"C4-2: charcoal_paper:enchanting failed ({type(e).__name__}: {e})")

# ---- C4-4: the right panel paper has green specks (owner 2026-11-16, "use a previous technique"): altar_panel's pure-paper
# 4x4 blocks are written as exact neutral greys (the RMI-096/RMI-091 encoder), the frame metal and gems keep their colour ----
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    rng3 = __import__("random").Random(5)
    panel = Image.new("RGBA", (128, 128))
    pp = panel.load()
    for y in range(128):
        for x in range(128):
            if x < 16 or x >= 112 or y < 16 or y >= 112:
                pp[x, y] = (60, 80, 140, 255)            # blue frame metal
            else:
                v = 190 + rng3.randint(-30, 30)          # light paper with strong grain (the real panel bakes to 16..33 greys)
                pp[x, y] = (v, v - 4, v - 12, 255)
    panel_dds = temp / "T_Enchantment_EnchantingBackground.dds"
    panel.save(panel_dds, pixel_format="DXT5")
    try:
        _, keys, bake = pipe.recipe("altar_panel:enchantingFrame")
        cols = {"enchantingFrame": [67.0, 75.0, 91.0], "altarPaper": [24.0, 25.0, 28.0]}
        out = Image.open(__import__("io").BytesIO(bake(panel_dds, cols, temp, None))).convert("RGB")
        paper_px = [out.getpixel((x, y)) for y in range(24, 104) for x in range(24, 104)]
        tinted = sum(1 for r, g, b in paper_px if max(r, g, b) - min(r, g, b) > 2)
        check(tinted == 0, f"C4-4: no tinted (green/cyan) pixel left in the panel paper ({tinted} of {len(paper_px)})")
        frame = out.getpixel((4, 64))
        check(frame[2] > frame[0] + 8, f"C4-4: the frame metal keeps its colour ({frame})")
    except Exception as e:   # noqa: BLE001
        check(False, f"C4-4: altar_panel failed ({type(e).__name__}: {e})")

# ---- C4-7 (owner 2026-11-16, "use the same techniques we used on the left parchment on the right one"): the right panel's
# paper = the page's parchment standard (inversion + blur 1.5 at the shared paper tone, neutral grey blocks); frame metal and
# gems unchanged; Enchanting only (Spellmaking's frame keeps altar_panel) ----
ench_assets = {a["asset"].split("/")[-1]: a for a in enchanting["assets"]}
bg = ench_assets.get("T_Enchantment_EnchantingBackground", {})
check(bg.get("recipe") == "altar_panel_invert:enchantingFrame:1.5", f"C4-7: the right panel uses altar_panel_invert ({bg.get('recipe')})")
check(sorted(bg.get("colours", [])) == ["enchantingFrame", "panel", "parchmentDark", "parchmentLight"],
      f"C4-7: it reads the frame metal and the page tone's colours ({bg.get('colours')})")
spm = json.loads((ROOT / "tools/menus/spellmaking.json").read_text(encoding="utf-8"))
check(any(a["recipe"] == "altar_panel:spellmakingFrame" for a in spm["assets"]), "C4-7: Spellmaking's frame keeps altar_panel")
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    panel_dds = temp / "T_Enchantment_EnchantingBackground.dds"
    panel.save(panel_dds, pixel_format="DXT5")
    try:
        _, keys, bake = pipe.recipe("altar_panel_invert:enchantingFrame:1.5")
        cols = {"enchantingFrame": [67.0, 75.0, 91.0], "parchmentDark": [8.0] * 3, "parchmentLight": [33.0] * 3, "panel": [41.0] * 3}
        out = Image.open(__import__("io").BytesIO(bake(panel_dds, cols, temp, None))).convert("RGB")
        paper_px = [out.getpixel((x, y)) for y in range(24, 104) for x in range(24, 104)]
        greys = sorted(p[1] for p in paper_px)
        check(abs(greys[len(greys) // 2] - 30.75) <= 3, f"C4-7: the paper median lands on the page tone ({greys[len(greys) // 2]})")
        check(greys[-len(greys) // 20] - greys[len(greys) // 20] >= 3, "C4-7: the paper keeps a grain (inverted, blurred)")
        check(sum(1 for r, g, b in paper_px if max(r, g, b) - min(r, g, b) > 2) == 0, "C4-7: the paper stays neutral grey")
        frame = out.getpixel((4, 64))
        check(frame[2] > frame[0] + 8, f"C4-7: the frame metal keeps its colour ({frame})")
    except Exception as e:   # noqa: BLE001
        check(False, f"C4-7: altar_panel_invert failed ({type(e).__name__}: {e})")

# ---- C4-9 / C4-10 (owner 2026-11-16, Sigil Stone: "no styling on the text, nor in the rows, add the same that is in the
# enchanting"): title menuTitle, right panel names + row names/values rowText (as Enchanting), rows 36 / icons 32 ----
bpc = enchanting["blueprint_colours"]
want = {("WBP_ModernMenu_SigilStoneMenu", 22052): "menuTitle", ("WBP_ModernMenu_SigilStoneMenu", 22152): "rowText",
        ("WBP_ModernMenu_SigilStoneMenu", 21989): "rowText", ("WBP_ModernMenu_GearEntry", 5976): "rowText",
        ("WBP_ModernMenu_GearEntry", 5841): "rowText", ("WBP_ModernMenu_GearEntry", 5909): "rowText",
        ("WBP_ModernMenu_GearEntry", 6047): "rowText"}
for (pkg, off), colour in want.items():
    row = next((r for r in bpc.get(pkg, []) if r["offset"] == off), None)
    check(row is not None and row["colour"] == colour, f"C4-9: {pkg} @{off} is {colour} ({row})")
for pkg in ("WBP_ModernMenu_SigilStoneMenu", "WBP_ModernMenu_GearEntry"):
    a = next((a for a in enchanting["assets"] if a["recipe"] == "bp_colour:" + pkg), None)
    check(a is not None and a["asset"] == "UI/Modern/GameMenuLayer/sigilstone/" + pkg, f"C4-9: {pkg} has its bp_colour asset row")
gear = layout["layout"].get("WBP_ModernMenu_GearEntry", {}).get("floats", [])
check(any(r["offset"] == 5676 and r["stock"] == 54.0 and r.get("size") == "sigilStoneRowHeight" for r in gear),
      "C4-10: the Sigil Stone row height (5676, stock 54) follows sigilStoneRowHeight")
check(sorted(r["offset"] for r in gear if r.get("widget") == "sigil_entry_icon") == [6508, 6516] and
      all(r.get("type") == "double" and r["stock"] == 48.0 and r.get("size") == "sigilStoneIconSize" for r in gear if r.get("widget") == "sigil_entry_icon"),
      "C4-10: the Sigil Stone icon (doubles 6508/6516, stock 48) follows sigilStoneIconSize")
check(cc.SIZES.get("sigilStoneRowHeight") == 36 and cc.SIZES.get("sigilStoneIconSize") == 32,
      f"C4-10: Sigil Stone rows default to 36 / icons 32 ({cc.SIZES.get('sigilStoneRowHeight')}, {cc.SIZES.get('sigilStoneIconSize')})")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
