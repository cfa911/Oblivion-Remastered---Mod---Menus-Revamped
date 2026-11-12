"""Container/Barter (and Inventory) side names, baked (owner 2026-11-12: "I dont like doing this type of stuff on runtime";
for every preset, Vanilla included, so it lives in the layout data: MRLayout_P, and MRCustom_P carries it, decision 46).

Cause (probe TEMP-DIAG C1C on C8FB448F, KB docs/inventory/internals.md "Container and barter widgets"): in
WBP_ModernMenu_ContainerInventory_MainContent the title retainer's HorizontalBoxSlot is Fill, so with the header row's wider
filter row it gets 0 width and the name is not drawn. Bake: the title slot Fill -> Auto (one stored byte, .uexp 18872) and the
Spacer_156 slot Auto -> Fill (an unstored Size added), so the title keeps its own width and the spacer takes the slack."""
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


sys.path.insert(0, str(source_paths.ROOT / "tools"))
from pack_builder import pipeline as pipe  # noqa: E402

PKG = "WBP_ModernMenu_ContainerInventory_MainContent"
row = pipe.LAYOUT.get(PKG, {})
check(row.get("asset") == "UI/Modern/GameMenuLayer/Container_Inventory/" + PKG, "the MainContent is a layout package (every preset)")
title = [b for b in row.get("bytes", []) if b.get("offset") == 18872]
check(len(title) == 1 and title[0].get("stock") == 1 and title[0].get("value") == 0,
      "the title retainer slot (HorizontalBoxSlot_2) SizeRule byte 18872: Fill 1 -> Auto 0")
spacer = [r for r in row.get("rewrites", []) if r.get("export_start") == 18901]
check(len(spacer) == 1 and spacer[0].get("add_bytes") == [[0, "010301"]] and spacer[0].get("stock_fields") == [4, 5]
      and spacer[0].get("export_size") == 14
      and spacer[0].get("stock_export_sha256") == "56c47b2e80ead67480022d7d1e6065d95413ee2c824b553ba7dc73c46ccdf3d0",
      "the Spacer_156 slot (HorizontalBoxSlot_7, export at 18901) gains Size {SizeRule Fill} as field 0")

# the byte patch, stock-checked like the floats
uexp = bytearray(19000)
uexp[18872] = 1
try:
    out = pipe.bp_float(bytes(uexp), PKG, {})
    check(out[18872] == 0, "bp_float bakes the title slot rule to Auto")
except Exception as error:   # noqa: BLE001
    check(False, f"bp_float bakes the byte rows ({error})")
uexp[18872] = 2
try:
    pipe.bp_float(bytes(uexp), PKG, {})
    check(False, "an unexpected stock rule byte is refused")
except pipe.SkipAsset:
    check(True, "")
except Exception as error:   # noqa: BLE001
    check(False, f"an unexpected stock rule byte is refused with SkipAsset ({error})")

# the added Size: the same bytes the game cooks for the title's own Fill slot (00 02 03 05 | 01 03 01 | parent | content)
stock_spacer = bytes.fromhex("0405") + struct.pack("<ii", 0x28, 0x43) + bytes(4)
added = pipe.add_field(stock_spacer, 0, {0: 3, 1: 0, 2: 1, 3: 1, 4: 4, 5: 4}, 0, bytes.fromhex("010301"))
check(added == bytes.fromhex("00020305" "010301") + struct.pack("<ii", 0x28, 0x43) + bytes(4),
      "add_field writes the Fill Size exactly like the game's cooked title slot")
pipeline_src = (source_paths.ROOT / "tools/pack_builder/pipeline.py").read_text(encoding="utf-8")
rewrite_src = pipeline_src[pipeline_src.find("def rewrite_package("):pipeline_src.find("def layout_package(")]
check('r.get("add_bytes"' in rewrite_src and "bytes.fromhex(" in rewrite_src, "rewrite_package adds raw struct bytes (add_bytes)")

# RMI-092 (owner 2026-11-12, "Bake width 300"): with the title Auto, each tab name's width pushed the filter row for a frame
# (probe C1I). The name gets a fixed minimum width (WBP_AltarTextBlock field 13 "Min Desired Width", float), wider than every
# tab name, so the filter row never moves. The colour is baked into the same export first: the stock check masks those bytes.
STOCK_TITLE = bytes.fromhex(
    "0206230300035803943dcc7b1c3da5dde83c0000803f00020205c9ffffff7e000000000000000000104202000000ff010000000900000043617465"
    "676f7279003f00000000000000")
width = [r for r in row.get("rewrites", []) if r.get("export_start") == 21270]
check(len(width) == 1 and width[0].get("add") == [[13, 300.0]] and width[0].get("stock_fields") == [2, 3, 4, 40]
      and width[0].get("export_size") == 72 and width[0].get("sha_mask") == [[6, 16]]
      and width[0].get("stock_export_sha256") == "b16a2152987ecf94a9697cd70e61a9d67fec2db43bafd3723a5387774d682bb0",
      "the title (inv_cont_title, export at 21270) gains Min Desired Width 300, its stock check masking the baked colour")
widened = pipe.add_field(STOCK_TITLE, 0, {0: 0, 1: 0, 2: 18, 3: 20, 4: 22}, 13, struct.pack("<f", 300.0))
check(widened[:6] == bytes.fromhex("0206" "0802" "1a03") and widened[66:70] == struct.pack("<f", 300.0)
      and widened[70:] == STOCK_TITLE[64:] and widened[6:66] == STOCK_TITLE[4:64] if len(widened) == 78 else False,
      "the width goes in as field 13, between the text and field 40, with the header re-fragmented")
masked = getattr(pipe, "masked_sha256", None)
recoloured = bytearray(STOCK_TITLE)
recoloured[6:18] = struct.pack("<3f", 0.9, 0.9, 0.9)
check(callable(masked) and masked(bytes(recoloured), [[6, 16]]) == masked(STOCK_TITLE, [[6, 16]])
      == "b16a2152987ecf94a9697cd70e61a9d67fec2db43bafd3723a5387774d682bb0"
      and masked(STOCK_TITLE, []) != masked(bytes(recoloured), []),
      "the masked stock check accepts the Custom colour and still sees every other byte")

print(f"{count} checks, {len(fails)} failures")
for why in fails:
    print("FAIL:", why)
sys.exit(1 if fails else 0)
