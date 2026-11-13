"""Custom pack: the row stolen mark baked into T_Icon_stolen (owner 2026-11-13: "bake the stolen icon too"; audit KB
mods/menus-revamped/evidence/inventory-runtime-audit-20261113.md item 2).

Before: in Custom (the only preset that recolours) the plugin swapped every row's native inv_status_stolen brush to our
Stolen-Item.png and tinted it kStolenFill at run time (InventoryHeaders.cpp PlaceRowStatus). Now the Pack Builder draws that
shape in a new colour entry stolenMark (default 210, 99, 91 = kStolenFill on the 245-grey icon, what the run-time tint showed)
into the game's T_Icon_stolen. Shared texture: the Enchanting rows (WBP_ModernMenu_EnchantmentEntry) show the same mark."""
import json
import re
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
WANT = [210.0, 99.0, 91.0]
sys.path.insert(0, str(ROOT / "tools"))
import custom_colours as cc  # noqa: E402
from pack_builder import pipeline as pipe  # noqa: E402

check(cc.COLOURS.get("stolenMark") == WANT, "stolenMark defaults to 210, 99, 91 (Dark default)")
check("stolen" in cc.HELP.get("stolenMark", "").lower(), "stolenMark has a plain label")
check(any("stolenMark" in r.get("members", ()) for r in cc.ROLES.values()), "a builder role feeds stolenMark")
ini = (ROOT / "config/CustomColours.ini").read_text(encoding="utf-8")
check(re.search(r"^stolenMark\s*=\s*210,\s*99,\s*91\b", ini, re.M) is not None, "the shipped CustomColours.ini has stolenMark")
check("MR_CC_RGB(stolenMark" in (ROOT / "src/core/rules/CustomColours.h").read_text(encoding="utf-8"), "the plugin reads stolenMark")
check(re.search(r"Rgb stolenMark\{\s*210,\s*99,\s*91\s*\}", (ROOT / "src/core/DarkStyle.h").read_text(encoding="utf-8")) is not None,
      "DarkStyle.h has the stolenMark default (decision 37)")

data = json.loads((ROOT / "tools/menus/inventory.json").read_text(encoding="utf-8"))
row = [a for a in data["assets"] if a["asset"] == "Art/UI/Icons/Dynamic_Icons/menus/Icons/T_Icon_stolen"]
check(len(row) == 1 and row[0]["recipe"] == "stolen_mark" and row[0]["colours"] == ["stolenMark"]
      and any("Enchanting" in s for s in row[0]["shared_with"]), "T_Icon_stolen is baked by stolen_mark (shared with Enchanting)")
check((ROOT / "tools/menus/Stolen-Item.png").is_file(), "the mark's shape ships with the builder data (tools/menus)")
check("*.png" in (ROOT / "tools/build_pack_builder.py").read_text(encoding="utf-8"), "the frozen builder copies the shape PNG")

# the recipe: the shape's alpha, every visible pixel in the colour
try:
    from PIL import Image
    numbers, colours, fn = pipe.recipe("stolen_mark")
    check(colours == ("stolenMark",), "stolen_mark takes stolenMark")
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        stock = work / "T_Icon_stolen.dds"
        Image.new("RGBA", (32, 32), (90, 40, 30, 255)).save(stock, pixel_format="DXT5")
        (work / "out.dds").write_bytes(fn(stock, {"stolenMark": WANT}, work, None))
        out = Image.open(work / "out.dds").convert("RGBA")
        shape = Image.open(ROOT / "tools/menus/Stolen-Item.png").convert("RGBA").resize((32, 32), Image.LANCZOS)
        solid = [p for p in out.get_flattened_data() if p[3] >= 200]
        check(bool(solid) and all(abs(p[0] - 210) <= 12 and abs(p[1] - 99) <= 12 and abs(p[2] - 91) <= 12 for p in solid),
              "opaque pixels take the stolen colour")
        corners = [out.getpixel(c)[3] for c in ((0, 0), (31, 0), (0, 31), (31, 31))]
        check(max(corners) <= 40, f"the corners outside the shape stay clear ({corners})")
        check(out.size == (32, 32), "the stock texture size is kept")
        cols = [x for x in range(32) if any(out.getpixel((x, y))[3] >= 128 for y in range(32))]
        check(cols and cols[-1] - cols[0] + 1 >= 29, f"the shape fills the icon width (owner: 80% of the sort-bar icon; the row box is 16, the toggle's shape ~20.6) ({cols[-1] - cols[0] + 1 if cols else 0} of 32)")
except Exception as error:   # noqa: BLE001
    check(False, f"stolen_mark draws the mark ({error})")

headers = (ROOT / "src/sorting/InventoryHeaders.cpp").read_text(encoding="utf-8-sig")
place = source_paths.function(headers, "void PlaceRowStatus(")
check("Stolen-Item.png" not in place and "kStolenFill" not in place, "no run-time brush swap or tint on the native row stolen icon")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
