"""Custom pack builder Phase 7 first batch (owner 2026-11-11, decisions 19, 22, 23): Enchanting and the dark-menu shared icons.
- Enchanting (tools/menus/enchanting.json): the page and its tab strips in the parchment greys, the tab icon in headerIcon,
  the decorated separator in columnLine, the gold preview background in the parchment greys; the text colour literals of
  WBP_ModernMenu_EnchanteStats, _EnchantmentEntry and _EnchantmentMenu in rowText and the SeparatorLine image tint in
  columnLine (owners proven from the export table, KB dark-mode-ui.md "Enchanting menu textures");
- the tab strips are BC1: parchment handles BC1 blocks and keeps each block's alpha mode (transparent pixels stay transparent);
- shared icons whose every user is already dark (Inventory/Magic filter icon tables) in headerIcon (tools/menus/common.json);
- each row reads exactly its recipe's keys; no asset is listed twice; no new colour keys."""
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


root = source_paths.ROOT
sys.path.insert(0, str(root / "tools"))
import custom_colours  # noqa: E402
import pack_builder.pipeline as pipe  # noqa: E402

from PIL import Image  # noqa: E402

ENCH = "Art/UI/Modern/GameMenuLayer/enchantment/"
TEXTURES = {
    ENCH + "Tabs/T_Enchantment_Tab_Paper": "parchment:enchanting",
    ENCH + "Tabs/T_Effects_icon": "flat:headerIcon", ENCH + "T_Enchantment_DecoratedSeparator": "flat:columnLine",
    ENCH + "T_Enchantment_Gold_Preview_Background": "parchment",
}
INK, BROWN, GOLD = (0.0723, 0.0382, 0.0284, 1.0), (0.1195, 0.0704, 0.0369, 1.0), (0.0999, 0.0561, 0.0395, 1.0)
BLUEPRINTS = {
    "UI/Modern/GameMenuLayer/Common/WBP_ModernMenu_EnchanteStats": [(o, BROWN, "rowText") for o in (9941, 10012, 10078, 10145)],
    "UI/Modern/GameMenuLayer/enchantment/WBP_ModernMenu_EnchantmentEntry": [(o, INK, "rowText") for o in (5065, 5132, 5205)],
    "UI/Modern/GameMenuLayer/enchantment/WBP_ModernMenu_EnchantmentMenu": [
        (63312, INK, "columnLine"), (73289, INK, "menuTitle"), (73395, INK, "menuTitle"), (73716, BROWN, "rowText"),
        (73789, GOLD, "rowText"), (73862, BROWN, "rowText"), (75158, INK, "panel"), (75493, INK, "panel")],   # banners: RMI-087
}
SHARED = ["Art/UI/Icons/Icon_Stats/T_Property_1_LesserPower", "Art/UI/Icons/Icon_Stats/T_Property_1_goodbad",
          "Art/UI/Icons/Icon_Stats/T_Property_1_school", "Art/UI/Icons/Icon_Stats/T_Property_Recharge"]

# ---- 1. the data rows ----
rows, seen = {}, []
for path in sorted((root / "tools/menus").glob("*.json")):
    data = json.loads(path.read_text(encoding="utf-8"))
    for row in data.get("assets", []):
        seen.append(row["asset"])
        rows[row["asset"]] = (path.stem, row)
check(len(seen) == len(set(seen)), "no asset is listed twice across the data files")
for asset, recipe in TEXTURES.items():
    menu, row = rows.get(asset, (None, {}))
    check(menu == "enchanting" and row.get("recipe") == recipe, f"{Path(asset).name}: enchanting.json, {recipe} ({menu}, {row.get('recipe')})")
for asset, literals in BLUEPRINTS.items():
    package = Path(asset).name
    menu, row = rows.get(asset, (None, {}))
    check(menu == "enchanting" and row.get("recipe") == f"bp_colour:{package}", f"{package}: enchanting.json, bp_colour ({menu}, {row.get('recipe')})")
    got = pipe.BLUEPRINT_COLOURS.get(package, [])
    check(len(got) == len(literals) and all(o == eo and c == ec and all(abs(a - b) < 5e-5 for a, b in zip(s, es))
                                            for (o, s, c), (eo, es, ec) in zip(got, literals)), f"{package}: the proven literals ({got})")
for asset in SHARED:
    menu, row = rows.get(asset, (None, {}))
    check(menu == "common" and row.get("recipe") == "flat:headerIcon", f"{Path(asset).name}: common.json, flat:headerIcon ({menu}, {row.get('recipe')})")
for asset, (menu, row) in rows.items():
    if menu not in ("enchanting", "common"):
        continue
    floats, rgbs, _ = pipe.recipe(row["recipe"])
    check(sorted(row.get("colours", [])) == sorted({*floats, *rgbs}), f"{Path(asset).name}: lists exactly the keys its recipe reads ({row.get('colours')})")
    for key in row.get("colours", []):
        check(key in custom_colours.DEFAULTS or key in custom_colours.BAKED_SKIN.values(), f"{Path(asset).name}: {key} is an existing key")

# ---- 2. parchment on a BC1 texture keeps size, header and each block's alpha mode ----


def bc1_texture(folder):
    image = Image.new("RGBA", (16, 16))
    image.putdata([(0, 0, 0, 0) if x < 4 else (180 + x * 3, 160 + y * 3, 120, 255) for y in range(16) for x in range(16)])
    path = Path(folder) / "T_Enchantment_Tab_Gear.dds"
    image.save(path, pixel_format="DXT1")
    return path


with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    try:
        stock_path = bc1_texture(temp)
        stock = stock_path.read_bytes()
        baked = pipe.recipe("parchment")[2](stock_path, {"parchmentDark": [30.0] * 3, "parchmentLight": [60.0] * 3}, temp, None)
        head = pipe.bc1_header_size(stock)
        check(head in (128, 148) and len(baked) == len(stock) and baked[:head] == stock[:head], "BC1: size and header kept")
        (temp / "out.dds").write_bytes(baked)
        out = Image.open(temp / "out.dds").convert("RGBA")
        check(all(out.getpixel((x, y))[3] == 0 for y in range(16) for x in range(4)), "BC1: transparent pixels stay transparent")
        opaque = [out.getpixel((x, y)) for y in range(16) for x in range(4, 16)]
        check(all(p[3] == 255 for p in opaque), "BC1: opaque pixels stay opaque")
        check(all(20 <= p[0] <= 70 and abs(p[0] - p[2]) <= 8 for p in opaque), f"BC1: opaque pixels in the parchment greys ({opaque[:3]})")
    except Exception as error:   # noqa: BLE001
        check(False, f"parchment handles a BC1 texture ({type(error).__name__}: {error})")

# ---- 3. a texture with bulk data (.ubulk, e.g. T_Effects_icon) is baked with its .ubulk ----
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    asset = ENCH + "Tabs/T_Effects_icon"
    legacy, stage, work = temp / "legacy", temp / "stage", temp / "work"
    src = legacy / "OblivionRemastered/Content" / asset
    src.parent.mkdir(parents=True)
    for ext, data in ((".uasset", b"uasset"), (".uexp", b"uexp"), (".ubulk", b"stock bulk")):
        src.with_suffix(ext).write_bytes(data)
    work.mkdir()
    dds = Image.new("RGBA", (8, 8), (90, 70, 50, 200))
    dds.save(temp / "T_Effects_icon.dds", pixel_format="DXT5")
    seen = {}

    def inject(uasset, dds_path, out):
        seen["ubulk"] = Path(uasset).with_suffix(".ubulk").is_file()   # UE4-DDS-Tools reads the .ubulk next to the .uasset
        Path(out).parent.mkdir(parents=True, exist_ok=True)
        for ext, data in ((".uasset", b"new uasset"), (".uexp", b"new uexp"), (".ubulk", b"new bulk")):
            Path(out).with_suffix(ext).write_bytes(data)
    try:
        pipe.bake(asset, "flat:headerIcon", custom_colours.pack_values(), temp, legacy, stage, work,
                  lambda name: temp / "T_Effects_icon.dds", inject)
        target = stage / "OblivionRemastered/Content" / asset
        check(seen.get("ubulk") is True, "the stock .ubulk is staged before the inject")
        check(target.with_suffix(".ubulk").read_bytes() == b"new bulk", "the injected .ubulk replaces the stock one in the stage")
    except Exception as error:   # noqa: BLE001
        check(False, f"a texture with a .ubulk bakes ({type(error).__name__}: {error})")

# ---- 4. a skipped asset whose stage file is still locked does not end the build ----
src_text = (root / "tools/pack_builder/pipeline.py").read_text(encoding="utf-8")
skip_block = src_text.split("except Exception as error:   # SkipAsset", 1)[-1].split("continue", 1)[0]
check("gc.collect()" in skip_block and "OSError" in skip_block,
      "the skip cleanup frees the failed inject's file handles and tolerates a locked stage file")

print(f"{count} checks, {len(fails)} failures")
for why in fails:
    print("FAIL:", why)
sys.exit(1 if fails else 0)
