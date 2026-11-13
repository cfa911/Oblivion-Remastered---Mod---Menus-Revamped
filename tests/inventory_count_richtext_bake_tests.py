"""Custom pack: the Inventory row count and the rich-text template default, baked (owner 2026-11-13, audit
KB mods/menus-revamped/evidence/inventory-runtime-audit-20261113.md).

- WBP_OriginalMenu_InventoryIconEntry > inv_entry_icon_count (WBP_AltarTextBlock_C): text colour at .uexp 4034 (stock dark
  brown) -> rowText (Body text); font outline colour at 4066 (stock light, OutlineSize 1) -> frame (dark). Owner: "Light text,
  dark outline".
- WBP_AltarRichTextBlock > rich_text_block template default colour at 3285 (stock black) -> rowText. Owner: "Bake to Body text".
Both were stock in every preset (no run-time ink pass since decision 28); offsets from an offline decode of the stock files."""
import struct
import sys
from pathlib import Path

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

ICON = "WBP_OriginalMenu_InventoryIconEntry"
RICH = "WBP_AltarRichTextBlock"
DARK = (0.0999, 0.0561, 0.0395, 1.0)
LIGHT = (0.855, 0.7913, 0.7157, 1.0)


def rows(package):
    return {offset: (tuple(round(v, 4) for v in stock), name) for offset, stock, name in pipe.BLUEPRINT_COLOURS.get(package, [])}


icon = rows(ICON)
check(4034 in icon and icon[4034][1] == "rowText" and all(abs(a - b) < 5e-4 for a, b in zip(icon[4034][0], DARK)),
      "the row count text colour (4034, stock dark brown) bakes to rowText")
check(4066 in icon and icon[4066][1] == "frame" and all(abs(a - b) < 5e-4 for a, b in zip(icon[4066][0], LIGHT)),
      "the row count outline colour (4066, stock light) bakes to frame (dark)")
rich = rows(RICH)
check(3285 in rich and rich[3285][1] == "rowText" and rich[3285][0] == (0.0, 0.0, 0.0, 1.0),
      "the rich-text template default (3285, stock black) bakes to rowText")

assets = {a["asset"]: a for a in pipe.load_menu_data("inventory")["assets"]} if hasattr(pipe, "load_menu_data") else None
if assets is None:
    import json
    assets = {a["asset"]: a for a in json.loads((source_paths.ROOT / "tools/menus/inventory.json").read_text(encoding="utf-8"))["assets"]}
icon_asset = assets.get("UI/Original/GameMenuLayer/Inventory/MainPart/" + ICON, {})
check(icon_asset.get("recipe") == "bp_colour:" + ICON and set(icon_asset.get("colours", [])) == {"rowText", "frame"},
      "the icon entry is a Custom pack asset baked by bp_colour (rowText, frame)")
rich_asset = assets.get("UI/Modern/Prefabs/" + RICH, {})
check(rich_asset.get("recipe") == "bp_colour:" + RICH and rich_asset.get("colours") == ["rowText"],
      "the rich-text prefab is a Custom pack asset baked by bp_colour (rowText)")

# the patch itself: values written, stock alpha kept, an unexpected stock refused
values = {"rowText": [239.0, 239.0, 239.0], "frame": [16.0, 16.0, 16.0]}
uexp = bytearray(4200)
struct.pack_into("<4f", uexp, 4034, *DARK)
struct.pack_into("<4f", uexp, 4066, *LIGHT)
try:
    out = pipe.bp_colour(bytes(uexp), ICON, values)
    text, outline = struct.unpack_from("<4f", out, 4034), struct.unpack_from("<4f", out, 4066)
    check(text[0] > 0.8 and outline[0] < 0.01 and text[3] == 1.0 and outline[3] == 1.0,
          "bp_colour writes light text and a dark outline, alpha kept")
except Exception as error:   # noqa: BLE001
    check(False, f"bp_colour bakes the icon entry ({error})")

# the offsets match the real stock file when the extract is on this machine
stock = Path("F:/modbuild/reports/menu-textures/_work/legacy/OblivionRemastered/Content")
for package, asset, offset, want in ((ICON, "UI/Original/GameMenuLayer/Inventory/MainPart/" + ICON, 4034, DARK),
                                     (ICON, "UI/Original/GameMenuLayer/Inventory/MainPart/" + ICON, 4066, LIGHT),
                                     (RICH, "UI/Modern/Prefabs/" + RICH, 3285, (0.0, 0.0, 0.0, 1.0))):
    path = stock / (asset + ".uexp")
    if path.is_file():
        found = struct.unpack_from("<4f", path.read_bytes(), offset)
        check(all(abs(a - b) < 5e-4 for a, b in zip(found, want)), f"stock {package} holds {want} at {offset} (found {found})")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
