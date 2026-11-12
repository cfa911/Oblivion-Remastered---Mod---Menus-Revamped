"""Custom pack builder decision 28, phase 28a (owner 2026-11-12: bake everything, no run-time recolouring of the game's widgets):
the Inventory, Container and Barter rows, footers and title, the shared row separator and equipped band, the hidden equipped
scribble and the Alchemy texts are baked; the run-time passes that inked or swapped them are gone.
KB: mods/menus-revamped/evidence/custom-pack-builder-plan.md "Decisions 28-29: phase plan";
docs/reverse-engineering/dark-mode-ui.md (owners found with probing/snippets/ink-literal-scan.py on the stock legacy export).
- the text colours are prefab-instance overrides (WBP_AltarTextBlock / WBP_Original_ImageAndText) stored in the owning blueprint:
  bp_colour to rowText (the colour the run-time ink used);
- T_List_Default (row separator, 12 row blueprints) flat:rowSeparator; T_List_equip_V2 (only the Focusbox prefab, 15 row
  blueprints) flat_alpha:rowEquipped:0.6 with the Focusbox equip tint set to white (alpha kept), as the run-time swap drew it;
- T_UI_Scribble (only M_UI_Scribble, only the Inventory row) cleared to transparent (the run-time alpha 0)."""
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

INK = [0.07227, 0.0382, 0.02843, 1.0]
WARM = [0.11954, 0.07036, 0.03689, 1.0]
WARM2 = [0.11954, 0.07036, 0.03955, 1.0]
IM, OR, AL = "UI/Original/GameMenuLayer/Inventory/MainPart/", "UI/Modern/GameMenuLayer/", "UI/Modern/GameMenuLayer/Alchemy/"
BLUEPRINTS = {   # package: (menu data file, asset path, [(offset, stock, colour)])
    "WBP_OriginalMenu_InventoryEntry": ("inventory", IM + "WBP_OriginalMenu_InventoryEntry", [(11935, INK, "rowText")]),
    "WBP_OriginalMenu_InventoryEntry_ValueWithLine": ("inventory", IM + "WBP_OriginalMenu_InventoryEntry_ValueWithLine",
                                                      [(1370, INK, "rowText")]),
    "WBP_OriginalMenu_InventoryMainPart": ("inventory", IM + "WBP_OriginalMenu_InventoryMainPart",
                                           [(19221, INK, "rowText"), (19343, INK, "rowText"), (19460, INK, "rowText")]),
    "WBP_ModernMenu_ContainerInventory_MainContent": ("inventory", OR + "Container_Inventory/WBP_ModernMenu_ContainerInventory_MainContent",
                                                      [(21276, INK, "rowText")]),
    "WBP_ModernPrefab_Focusbox": ("common", "UI/Modern/Prefabs/WBP_ModernPrefab_Focusbox",
                                  [(5642, [0.49102, 0.42869, 0.36625, 0.35], "white")]),
    "WBP_ModernMenu_AlchemyMenu": ("alchemy", AL + "WBP_ModernMenu_AlchemyMenu",
                                   [(67439, INK, "rowText"), (67540, WARM, "rowText"), (67764, WARM2, "rowText"), (67835, WARM2, "rowText")]),
    "WBP_ModernMenu_AlchemyFilters": ("alchemy", AL + "Filters/WBP_ModernMenu_AlchemyFilters",
                                      [(14356, [0.07421, 0.0382, 0.02732, 1.0], "rowText")]),
    "WBP_ModernMenu_Alchemy_FilterEffectButton": ("alchemy", AL + "Filters/WBP_ModernMenu_Alchemy_FilterEffectButton", [(790, INK, "rowText")]),
    "WBP_ModernMenu_UstensilEntry": ("alchemy", AL + "WBP_ModernMenu_UstensilEntry", [(4170, INK, "rowText")]),
    "WBP_ModernMenu_AlchemyItemEntry": ("alchemy", AL + "WBP_ModernMenu_AlchemyItemEntry",
                                        [(5950, INK, "rowText"), (6002, [0.10224, 0.05781, 0.04092, 1.0], "rowText")]),
}
ADDED = {   # literals added to blueprints the pack already patches
    "WBP_ModernMenu_FilterEffectEntry": ("alchemy", [(2617, INK, "rowText")]),
    "WBP_ModernPrefab_SelectableButton": ("alchemy", [(1494, INK, "rowText")]),
    "WBP_LegacyMenu_AddedEffects": ("alchemy", [(18478, WARM, "rowText")]),
}
TEXTURES = {
    "Art/UI/Common/T_List_Default": ("common", "flat:rowSeparator", ["rowSeparator"]),
    "Art/UI/Common/T_List_equip_V2": ("common", "flat_alpha:rowEquipped:0.6", ["rowEquipped"]),
    "Art/UI/Common/Buttons/T_UI_Scribble": ("inventory", "clear", []),
}

menus = {p.stem: json.loads(p.read_text(encoding="utf-8")) for p in sorted((root / "tools/menus").glob("*.json"))}
rows = {row["asset"]: (menu, row) for menu, data in menus.items() for row in data.get("assets", [])}

# ---- 1. the colour keys ----
dark = custom_colours.from_darkstyle((root / "src/core/DarkStyle.h").read_text(encoding="utf-8"))
reader = (root / "src/core/rules/CustomColours.h").read_text(encoding="utf-8")
check(custom_colours.COLOURS.get("rowEquipped") == [212.0, 170.0, 78.0], f"rowEquipped default = the run-time band gold ({custom_colours.COLOURS.get('rowEquipped')})")
check(custom_colours.HELP.get("rowEquipped", "").startswith("Equipped"), "rowEquipped is labelled")
check(dark.get("rowEquipped") == [212.0, 170.0, 78.0], "DarkStyle.h holds rowEquipped")
check("MR_CC_RGB(rowEquipped," in reader, "the plugin reader knows rowEquipped")
check("rowSeparator and pickerIcon are retained" not in custom_colours.render(custom_colours.DEFAULTS),
      "rowSeparator is no longer a retained key with no effect (it colours the baked row separator)")
check("rowSeparator and pickerIcon are retained" not in reader, "the plugin's default file agrees")
check((root / "config/CustomColours.ini").read_text(encoding="utf-8") == custom_colours.render(custom_colours.DEFAULTS),
      "config/CustomColours.ini is the rendered default file")
check(custom_colours.GROUPS.get("rowEquipped") == "Inventory and Magic lists", "rowEquipped is in the lists group")

# ---- 2. the data rows ----
for asset, (menu, recipe, colours) in TEXTURES.items():
    got_menu, row = rows.get(asset, (None, {}))
    check(got_menu == menu and row.get("recipe") == recipe, f"{Path(asset).name}: {menu}.json {recipe} ({got_menu}, {row.get('recipe')})")
    check(sorted(row.get("colours", [])) == sorted(colours), f"{Path(asset).name}: reads {colours} ({row.get('colours')})")
    check(bool(row.get("shared_with")), f"{Path(asset).name}: lists the other menus it reaches")
    try:
        floats, rgbs, _ = pipe.recipe(recipe)
        check(sorted({*floats, *rgbs}) == sorted(colours), f"{recipe} reads exactly {colours}")
    except Exception as error:   # noqa: BLE001
        check(False, f"pipeline.recipe({recipe!r}) exists ({type(error).__name__}: {error})")


def literal_rows(menu, package):
    return {r["offset"]: r for r in menus.get(menu, {}).get("blueprint_colours", {}).get(package, [])}


for package, (menu, asset, literals) in BLUEPRINTS.items():
    got = literal_rows(menu, package)
    for offset, stock, colour in literals:
        r = got.get(offset, {})
        check(r.get("colour") == colour and all(abs(a - b) < 2e-4 for a, b in zip(r.get("stock", []), stock)) and len(r.get("stock", [])) == 4,
              f"{package} @{offset}: stock {stock} -> {colour} ({r})")
    menu_row = rows.get(asset, (None, {}))
    check(menu_row[0] == menu and menu_row[1].get("recipe") == f"bp_colour:{package}", f"{package}: an asset row in {menu}.json ({menu_row[0]}, {menu_row[1].get('recipe')})")
    check(bool(menu_row[1].get("shared_with")), f"{package}: lists the menus it reaches")
for package, (menu, literals) in ADDED.items():
    got = literal_rows(menu, package)
    for offset, stock, colour in literals:
        r = got.get(offset, {})
        check(r.get("colour") == colour and all(abs(a - b) < 2e-4 for a, b in zip(r.get("stock", []), stock)), f"{package} @{offset}: added -> {colour} ({r})")

# ---- 3. the recipes ----
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    band = Image.new("RGBA", (16, 16))
    band.putdata([(183, 175, 162, 255 if y > 3 else 17 * x) for y in range(16) for x in range(16)])
    path = temp / "band.dds"
    band.save(path, pixel_format="DXT5")
    stock_image = Image.open(path).convert("RGBA")
    try:
        out_bytes = pipe.recipe("flat_alpha:rowEquipped:0.6")[2](path, {"rowEquipped": [212.0, 170.0, 78.0]}, temp, None)
        (temp / "o.dds").write_bytes(out_bytes)
        out = Image.open(temp / "o.dds").convert("RGBA")
        check(len(out_bytes) == path.stat().st_size, "flat_alpha: size kept")
        px = [out.getpixel((x, y)) for y in range(16) for x in range(16)]
        check(all(abs(p[0] - 212) <= 8 and abs(p[1] - 170) <= 8 and abs(p[2] - 78) <= 8 for p in px), f"flat_alpha: every pixel the colour ({px[200]})")
        check(all(abs(out.getpixel((x, y))[3] - round(stock_image.getpixel((x, y))[3] * 0.6)) <= 3 for y in range(16) for x in range(16)),
              "flat_alpha: alpha = stock alpha x 0.6")
    except Exception as error:   # noqa: BLE001
        check(False, f"flat_alpha works ({type(error).__name__}: {error})")
    try:
        out_bytes = pipe.recipe("clear")[2](path, {}, temp, None)
        (temp / "c.dds").write_bytes(out_bytes)
        out = Image.open(temp / "c.dds").convert("RGBA")
        check(len(out_bytes) == path.stat().st_size, "clear: size kept")
        check(all(out.getpixel((x, y))[3] == 0 for y in range(16) for x in range(16)), "clear: every pixel transparent")
    except Exception as error:   # noqa: BLE001
        check(False, f"clear works ({type(error).__name__}: {error})")

# bp_colour: a fixed "white" (no colour-file key) keeps the stock alpha; the recipe reads no key for it
try:
    uexp = bytearray(6000)
    struct.pack_into("<4f", uexp, 5642, 0.49102, 0.42869, 0.36625, 0.35)
    out = struct.unpack_from("<4f", pipe.bp_colour(bytes(uexp), "WBP_ModernPrefab_Focusbox", {}), 5642)
    check(all(abs(c - 1.0) < 1e-6 for c in out[:3]) and abs(out[3] - 0.35) < 1e-6, f"bp_colour white keeps the alpha ({out})")
    check(pipe.recipe("bp_colour:WBP_ModernPrefab_Focusbox")[1] == (), "the Focusbox recipe reads no colour-file key")
except Exception as error:   # noqa: BLE001
    check(False, f"bp_colour patches the Focusbox ({type(error).__name__}: {error})")

# ---- 4. the run-time passes are gone ----
skin = (root / "src/sorting/MenuSkin.cpp").read_text(encoding="utf-8")
skin_h = (root / "src/sorting/MenuSkin.h").read_text(encoding="utf-8")
headers = (root / "src/sorting/InventoryHeaders.cpp").read_text(encoding="utf-8")
theme = (root / "src/core/UiTheme.h").read_text(encoding="utf-8")
alchemy = (root / "src/alchemy/AlchemyMenu.cpp").read_text(encoding="utf-8") + (root / "src/alchemy/AlchemyLayout.cpp").read_text(encoding="utf-8")
for name in ("StyleRow(", "InkFooter", "WatchFooters", "InkTree(", "hideEquippedScribble", "inv_entry_scribble"):
    check(name not in skin, f"MenuSkin.cpp: {name} removed")
for name in ("StyleRow(", "WatchFooters(", "InkTree("):
    check(name not in skin_h, f"MenuSkin.h: {name} removed")
    check(name not in headers, f"InventoryHeaders.cpp: no {name} call")
check("InkTree(" not in alchemy, "Alchemy: no run-time ink pass")
title = skin[skin.find("void StyleTitle("):skin.find("void SetTitleLabel(")]
check("void StyleTitle(" in skin and "uix::Ink" not in title and "UnclipTitle(title)" in title, "StyleTitle keeps the layout (RMI-073), no ink")
for stock in ("T_List_Default", "T_List_equip_V2"):
    check(f'"{stock}"' not in theme[theme.find("kDarkArt[]"):theme.find("};", theme.find("kDarkArt[]"))], f"kDarkArt: no {stock} swap (baked)")
check("hideEquippedScribble" not in theme, "UiTheme.h: no hideEquippedScribble")

print(f"{count} checks, {len(fails)} failures")
for why in fails:
    print("FAIL:", why)
sys.exit(1 if fails else 0)
