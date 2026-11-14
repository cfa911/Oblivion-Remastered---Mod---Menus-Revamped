"""Magic page title: baked like the Inventory/container title, no run-time title code (Step C.2; owner: no run-time title
labels; the RMI-079 label replaced).

Cause (probe MRMAGIC8, KB docs/inventory/magic-header-geometry-2026-10-30.md): in WBP_ModernMenu_MagicMenu the title row
Header_Box holds [Magic_TabName, Magic_Filters, Filters_spacer]; the title's HorizontalBoxSlot_1 is Fill (padding left 18), so
the header row's wider filter row squeezes it to 0 and the page name is not drawn. Offline decode of the stock package (game
1.512.105.0): HorizontalBoxSlot_1 export at .uexp 62174 (25 bytes, fields 0 1 4 5), SizeRule byte at 62180 = 1 (Fill);
Magic_TabName (WBP_AltarTextBlock_C) export at 74018 (72 bytes, fields 2 3 4 40, the same shape as inv_cont_title), colour at
74024 (0.0723, 0.0382, 0.0284). Bake: slot Auto, Min Desired Width 300 (the label's width: the filters never move per page),
the Custom colour rowText (only uPreset 2 recolours, and it always carries MRCustom_P)."""
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

PKG = "WBP_ModernMenu_MagicMenu"
row = pipe.LAYOUT.get(PKG, {})
slot = [b for b in row.get("bytes", []) if b.get("offset") == 62180]
check(len(slot) == 1 and slot[0].get("stock") == 1 and slot[0].get("value") == 0,
      "Magic_TabName's slot (HorizontalBoxSlot_1) SizeRule byte 62180: Fill 1 -> Auto 0 (every preset)")
width = [r for r in row.get("rewrites", []) if r.get("export_start") == 74018]
check(len(width) == 1 and width[0].get("export_size") == 72 and width[0].get("stock_fields") == [2, 3, 4, 40]
      and width[0].get("add") == [[13, 300.0]] and width[0].get("sha_mask") == [[6, 16]]
      and width[0].get("stock_export_sha256") == "47e39aee9bf5708cbee1cdc2dbb5b3c7c71738a40c5b2604e9df4afed756c2db",
      "Magic_TabName gains Min Desired Width 300 (field 13), stock-checked with its colour masked")

# the byte patch is stock-checked like the floats
uexp = bytearray(75000)
uexp[62180] = 1
for r in row.get("floats", []):   # the menu's other layout floats need their stock values to pass
    import struct
    struct.pack_into("<f", uexp, r["offset"], r["stock"])
try:
    out = pipe.bp_float(bytes(uexp), PKG, {"font.MagicMenu.Magic_SpellEffectivenes_Value": 21.0})
    check(out[62180] == 0, "bp_float bakes the Magic title slot to Auto")
except Exception as error:   # noqa: BLE001
    check(False, f"bp_float bakes the Magic title slot ({error})")

# the title colour, Custom only (bp_colour asset in the Magic data file)
import json  # noqa: E402

magic = json.loads((ROOT / "tools/menus/magic.json").read_text(encoding="utf-8"))
check(any(o == 74024 and tuple(s) == (0.0723, 0.0382, 0.0284, 1.0) and c == "rowText"
          for o, s, c in pipe.BLUEPRINT_COLOURS.get(PKG, [])), "the pipeline loads the Magic title colour row")
colours = magic.get("blueprint_colours", {}).get(PKG, [])
check(any(c.get("offset") == 74024 and c.get("colour") == "rowText" and c.get("widget") == "Magic_TabName"
          and c.get("stock") == [0.0723, 0.0382, 0.0284, 1.0] for c in colours),
      "Magic_TabName's colour (74024) is baked to rowText in the Custom pack")
check(any(a.get("recipe") == "bp_colour:" + PKG for a in magic.get("assets", [])),
      "the Magic menu blueprint is a Custom colour asset")

# no run-time Magic title code
headers = (ROOT / "src/sorting/InventoryHeaders.cpp").read_text(encoding="utf-8-sig")
tick = (ROOT / "src/sorting/PickerTick.cpp").read_text(encoding="utf-8-sig")
internal = (ROOT / "src/sorting/PickerInternal.h").read_text(encoding="utf-8-sig")
skin = (ROOT / "src/sorting/MenuSkin.cpp").read_text(encoding="utf-8-sig")
skin_h = (ROOT / "src/sorting/MenuSkin.h").read_text(encoding="utf-8-sig")
rules = (ROOT / "src/sorting/rules/InventoryHeaderLayout.h").read_text(encoding="utf-8-sig")
check("PlaceMagicTitleLabel" not in headers and "MrMagicTitleLabel" not in headers, "no Magic title label at run time")
check("SyncMagicTitle" not in headers + tick + internal, "no per-tick Magic title copy")
check("magicTitleText" not in headers + internal and "titleNative" not in headers + internal
      and "titleLabel" not in headers + internal, "no Magic title handles")
check('"Magic_TabName"' not in headers, "the header code does not touch the native Magic title")
check("InkText" not in skin + skin_h + headers, "the label-only InkText helper is gone")
check("kTitleLabelWidth" not in rules + headers, "the label width constant is gone")
check("MR-MAGIC-TITLE-BAKED" in headers, "the build carries the C.2 marker")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
