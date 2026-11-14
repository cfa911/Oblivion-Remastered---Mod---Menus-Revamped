"""Custom pack builder decision 28, phase 28b (owner 2026-11-12: bake everything, no run-time recolouring of the game's widgets):
the Inventory and Magic item details (and what they share: the skill sub-tab = Character skills, the Magic details = Spell
Purchase) are baked; the run-time details passes (StyleDetails*, the effect-line and perk-line passes, the action button pass)
are gone.
KB: mods/menus-revamped/evidence/custom-pack-builder-plan.md "Decisions 28-29: phase plan" and "Phase 28b";
docs/reverse-engineering/dark-mode-ui.md "Item details colour owners" (owners from probing/snippets/ink-literal-scan.py and
image-tile-refs.py on the stock legacy export, build 1.512.105.0).
- details texts: prefab-instance overrides in the owning blueprint, bp_colour to rowText (the run-time ink);
- ornament lines (T_StatsMenu_Line_R, T_decoration_line tile tints): bp_colour to the [Skin] ornament value (linear, as the
  run-time widget tint drew it over a neutral brush); the Effects/skill title band tiles: white (the run-time swap drew the
  art untinted); perk lines: rowText, the perk table's emphasis row: [Skin] emphasisInk; level ring: MIC_UI_SkillCircularProgress
  Color override: [Skin] ring;
- art swaps become texture recipes: grey_scale:<k> (grey = Rec.709 brightness x k, alpha kept; the former run-time swap art's gray(k)),
  skin_flat:ornament:235 (the description rules: the white 235 RowSeparator art times the ornament tint), flat:headerIcon
  (T_Property_1_Range, like its baked siblings);
- [Skin] ornament, ring, emphasisInk become baked entries; band, buttonFill, buttonRule are retired (no effect: the band art and
  the button prefab are baked)."""
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
from pack_builder import model as pbmodel  # noqa: E402

from PIL import Image  # noqa: E402

INK = [0.07227, 0.0382, 0.02843, 1.0]
WARM = [0.11954, 0.07036, 0.03689, 1.0]
GOLD = [0.0999, 0.05613, 0.03955, 1.0]
ORN = [0.2502, 0.1845, 0.1119, 1.0]
BAND = [0.2918, 0.1590, 0.0908, 1.0]
SHADOW_AT = 11509
ID, IS = "UI/Original/GameMenuLayer/Inventory/ItemDetails/", "UI/Original/GameMenuLayer/Inventory/ItemDetails/Specific/"
P3 = "UI/Modern/GameMenuLayer/Stats/Page3_Skills/"
BLUEPRINTS = {   # package: (menu data file, asset path, [(offset, stock, colour)])
    "WBP_OriginalMenu_InventoryItemDetails": ("inventory", ID + "WBP_OriginalMenu_InventoryItemDetails",
                                              [(45984, INK, "rowText"), (46054, INK, "rowText"), (46127, INK, "rowText"),
                                               (46544, ORN, "Skin.ornament"), (46632, ORN, "Skin.ornament")]),
    "WBP_OriginalMenu_InventoryMainContent": ("inventory", ID + "WBP_OriginalMenu_InventoryMainContent", [(845, INK, "rowText")]),
    "WBP_OriginalMenu_InventoryEffectsTitle": ("inventory", IS + "WBP_OriginalMenu_InventoryEffectsTitle",
                                               [(196, INK, "rowText"), (315, BAND, "white")]),
    "WBP_OriginalMenu_InventoryItemPreview": ("inventory", ID + "WBP_OriginalMenu_InventoryItemPreview",
                                              [(88293, INK, "rowText"), (88406, INK, "rowText"), (88518, INK, "rowText"),
                                               (88628, INK, "rowText")]),
    "WBP_OriginalMenu_InventoryItemSpecificContentWeapon": ("inventory", IS + "WBP_OriginalMenu_InventoryItemSpecificContentWeapon",
                                                            [(7025, INK, "rowText"), (7075, INK, "rowText")]),
    "WBP_OriginalMenu_InventoryItemSpecificContentSigilStone": ("inventory", IS + "WBP_OriginalMenu_InventoryItemSpecificContentSigilStone",
                                                                [(1436, INK, "rowText"), (1548, INK, "rowText"),
                                                                 (1852, [0.16202, 0.10946, 0.07819, 1.0], "Skin.ornament")]),
    "WBP_OriginalMenu_IngredientEffect": ("inventory", IS + "WBP_OriginalMenu_IngredientEffect",
                                          [(2687, INK, "rowText"), (2834, INK, "rowText")]),
    "WBP_ModernMenu_StatsPage3_Details": ("inventory", P3 + "WBP_ModernMenu_StatsPage3_Details",
                                          [(23699, WARM, "rowText"), (24034, INK, "rowText"), (24104, INK, "rowText"),
                                           (24266, INK, "rowText"), (24284, INK, "rowText"), (24431, ORN, "Skin.ornament"),
                                           (24519, ORN, "Skin.ornament"), (24947, BAND, "white")]),
    "WBP_ModernMenu_Stats_LevelDescription": ("inventory", P3 + "WBP_ModernMenu_Stats_LevelDescription",
                                              [(5352, INK, "rowText"), (5415, INK, "rowText"), (5485, INK, "rowText"),
                                               (5581, WARM, "rowText"), (5600, WARM[:3] + [0.5], "rowText")]),
    "DT_StatsMenu_SkillDescriptionRichTextStyle_PerLevelDescription": (
        "inventory", "UI/Legacy/GameMenuLayer/stats/DataTables/DT_StatsMenu_SkillDescriptionRichTextStyle_PerLevelDescription",
        [(69, WARM, "rowText"), (425, WARM, "Skin.emphasisInk")]),
    "WBP_ModernMenu_MagicDetails": ("magic", "UI/Modern/GameMenuLayer/Magic/WBP_ModernMenu_MagicDetails",
                                    [(18693, INK, "menuTitle"), (18806, INK, "accentValue"), (18875, INK, "effectLabel"), (18982, INK, "menuTitle"),
                                     (19064, INK, "rowText"), (19374, INK, "menuTitle"), (19592, INK, "menuTitle")]
                                    + [(o, GOLD, "rowText") for o in (18589, 19139, 19212, 19304, 19487, 19678, 19781, 19849,
                                                                       19924, 20031, 20098)]),
}
MATERIALS = {   # material instance: (menu, asset, [(offset, stock, colour)])
    "MIC_UI_SkillCircularProgress": ("inventory", "UI/Materials/Instances/MIC_UI_SkillCircularProgress",
                                     [(169, [0.86316, 0.3564, 0.13014, 1.0], "Skin.ring")]),
}
RULE = "skin_flat:ornament:235"
TEXTURES = {   # asset: (menu, recipe, colour keys)
    "Art/UI/Icons/Icon_Stats/T_Property_1_Range": ("alchemy", "flat:headerIcon", ["headerIcon"]),
    "Art/UI/Modern/GameMenuLayer/Inventory/T_inv_details_effects_title_BG": ("inventory", "grey_scale:0.45", []),
    "Art/UI/Modern/GameMenuLayer/SigilStone/T_SigilStone_SubNameBG": ("inventory", "grey_scale:0.45", []),
    "Art/UI/Icons/Icon_Category/T_Category_Armor": ("inventory", "grey_scale:2.2", []),
    "Art/UI/Icons/Icon_Category/T_Category_Weapons": ("inventory", "grey_scale:1.8", []),
    "Art/UI/Modern/GameMenuLayer/Stats/T_CircularProgress_bg": ("inventory", "grey_scale:1.0", []),
    "Art/UI/Common/T_Line_Brown": ("inventory", RULE, ["Skin.ornament"]),
    "Art/UI/Common/T_Diamond_Brown_Separator": ("magic", RULE, ["Skin.ornament"]),
    "Art/UI/Modern/GameMenuLayer/CharacterCreation/T_Character_Line_Long": ("magic", RULE, ["Skin.ornament"]),
    "Art/UI/Common/T_Line_02": ("inventory", RULE, ["Skin.ornament"]),
}

menus = {p.stem: json.loads(p.read_text(encoding="utf-8")) for p in sorted((root / "tools/menus").glob("*.json"))}
rows = {row["asset"]: (menu, row) for menu, data in menus.items() for row in data.get("assets", [])}

# ---- 1. the colour keys: three [Skin] tints baked, three retired ----
for name in ("ornament", "ring", "emphasisInk"):
    check(custom_colours.BAKED_SKIN.get(name) == f"Skin.{name}", f"[Skin] {name} is baked (Skin.{name})")
    check(any(f"Skin.{name}" in r["members"] for r in custom_colours.ROLES.values()), f"Skin.{name} is fed by a builder role")
retired = getattr(custom_colours, "RETIRED_SKIN", ())
check(tuple(retired) == ("band", "buttonFill", "buttonRule"), f"retired [Skin] keys ({retired})")
values = custom_colours.pack_values()
for key in ("Skin.ornament", "Skin.ring", "Skin.emphasisInk"):
    check(key in values, f"pack_values has {key}")
rendered = custom_colours.render(custom_colours.DEFAULTS)
reader = (root / "src/core/rules/CustomColours.h").read_text(encoding="utf-8")
NOTE = "[Skin] band, buttonFill and buttonRule are retained with no effect"
check(NOTE in rendered and NOTE in reader, "the default file (Python and plugin) says the three retired [Skin] keys do nothing")
check((root / "config/CustomColours.ini").read_text(encoding="utf-8") == rendered, "config/CustomColours.ini is the rendered default file")
try:
    m = pbmodel.ColourModel(Path(tempfile.gettempdir()) / "mr-28b-missing.ini")
    for name in ("band", "buttonFill", "buttonRule"):
        check(m.entry("Skin", name).applies == "Inactive", f"model: [Skin] {name} Inactive ({m.entry('Skin', name).applies})")
    for name in ("ornament", "ring", "emphasisInk"):
        check(m.entry("Skin", name).applies == "Build pack", f"model: [Skin] {name} needs a Build ({m.entry('Skin', name).applies})")
    shown = {e.key.split(".")[-1] for label, groups in m.pages() if label in ("Colours", "Runtime") for _, entries in groups for e in entries}
    check(not shown & {"band", "buttonFill", "buttonRule"}, f"window: the retired keys are not shown ({shown & {'band', 'buttonFill', 'buttonRule'}})")
    run_time = [entries for label, groups in m.pages() if label == "Runtime" for _, entries in groups]
    check(sorted(e.name for entries in run_time for e in entries) == sorted(["guide", "rule", "alchemyHeading", "alchemyBox", "alchemyBoxLine", "alchemyClearLine"]),
      "window: only our own widgets stay run time (header rule and guide; C.3 round 2: the Alchemy filter heading, boxes and outlines)")
    check({"Skin.ornament", "Skin.ring", "Skin.emphasisInk"} <= set(m.needs_build({})), "an older pack without the baked tints needs a Build")
except Exception as error:   # noqa: BLE001
    check(False, f"model works ({type(error).__name__}: {error})")

# ---- 2. the data rows ----
for asset, (menu, recipe, colours) in TEXTURES.items():
    got_menu, row = rows.get(asset, (None, {}))
    check(got_menu == menu and row.get("recipe") == recipe, f"{Path(asset).name}: {menu}.json {recipe} ({got_menu}, {row.get('recipe')})")
    check(sorted(row.get("colours", [])) == sorted(colours), f"{Path(asset).name}: reads {colours} ({row.get('colours')})")
    check(bool(row.get("shared_with")), f"{Path(asset).name}: lists the other menus it reaches")
    try:
        floats, rgbs, _ = pipe.recipe(recipe)
        check(sorted({*floats, *rgbs}) == sorted(colours), f"{recipe} reads exactly {colours} ({floats}, {rgbs})")
    except Exception as error:   # noqa: BLE001
        check(False, f"pipeline.recipe({recipe!r}) exists ({type(error).__name__}: {error})")


def literal_rows(menu, section, package):
    return {r["offset"]: r for r in menus.get(menu, {}).get(section, {}).get(package, [])}


for section, kind, table in (("blueprint_colours", "bp_colour", BLUEPRINTS), ("material_colours", "material_colour", MATERIALS)):
    for package, (menu, asset, literals) in table.items():
        got = literal_rows(menu, section, package)
        check(len(got) == len(literals), f"{package}: {len(literals)} literals in {menu}.json {section} ({len(got)})")
        for offset, stock, colour in literals:
            r = got.get(offset, {})
            check(r.get("colour") == colour and len(r.get("stock", [])) == 4 and all(abs(a - b) < 2e-4 for a, b in zip(r.get("stock", []), stock)),
                  f"{package} @{offset}: stock {stock} -> {colour} ({r})")
        menu_row = rows.get(asset, (None, {}))
        check(menu_row[0] == menu and menu_row[1].get("recipe") == f"{kind}:{package}",
              f"{package}: an asset row in {menu}.json ({menu_row[0]}, {menu_row[1].get('recipe')})")
        check(bool(menu_row[1].get("shared_with")), f"{package}: lists the menus it reaches")

# ---- 3. the recipes ----
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    art = Image.new("RGBA", (16, 16))
    art.putdata([(160, 128, 105, 16 * (x % 16)) if y > 7 else (200, 200, 200, 255) for y in range(16) for x in range(16)])
    path = temp / "art.dds"
    art.save(path, pixel_format="DXT5")
    stock = Image.open(path).convert("RGBA")
    stock_bytes = path.read_bytes()
    try:
        out_bytes = pipe.recipe("grey_scale:0.45")[2](path, {}, temp, None)
        (temp / "g.dds").write_bytes(out_bytes)
        out = Image.open(temp / "g.dds").convert("RGBA")
        check(len(out_bytes) == len(stock_bytes), "grey_scale: size kept")
        check(all(out_bytes[o:o + 8] == stock_bytes[o:o + 8] for o in range(128, len(stock_bytes), 16)), "grey_scale: alpha blocks byte for byte")
        p, s = out.getpixel((3, 12)), stock.getpixel((3, 12))
        want = pipe.lum(s) * 0.45
        check(max(p[:3]) - min(p[:3]) <= 4 and abs(p[1] - want) <= 6, f"grey_scale: a neutral grey of brightness x 0.45 ({p}, want {want:.0f})")
        p = out.getpixel((3, 3))
        check(abs(p[1] - min(255, 200 * 0.45)) <= 6, f"grey_scale: the light part too ({p})")
        hi = pipe.recipe("grey_scale:2.2")[2](path, {}, temp, None)
        (temp / "h.dds").write_bytes(hi)
        check(Image.open(temp / "h.dds").convert("RGBA").getpixel((3, 3))[1] >= 248, "grey_scale: clamped at white")
    except Exception as error:   # noqa: BLE001
        check(False, f"grey_scale works ({type(error).__name__}: {error})")
    try:
        skin = {"Skin.ornament": [0.45, 0.45, 0.45, 1.0]}
        out_bytes = pipe.recipe(RULE)[2](path, skin, temp, None)
        (temp / "r.dds").write_bytes(out_bytes)
        out = Image.open(temp / "r.dds").convert("RGBA")
        want = pipe.srgb(pipe.linear(235) * 0.45)
        px = [out.getpixel((x, y)) for y in range(16) for x in range(16)]
        check(all(abs(q[i] - want) <= 8 for q in px for i in range(3)), f"skin_flat: 235 x the ornament tint ({px[0]}, want {want})")
        check(all(out_bytes[o:o + 8] == stock_bytes[o:o + 8] for o in range(128, len(stock_bytes), 16)), "skin_flat: alpha kept")
        tinted = pipe.recipe(RULE)[2](path, {"Skin.ornament": [0.9, 0.2, 0.1, 1.0]}, temp, None)
        (temp / "t.dds").write_bytes(tinted)
        q = Image.open(temp / "t.dds").convert("RGBA").getpixel((0, 0))
        check(q[0] > q[1] > q[2], f"skin_flat: a coloured ornament tint gives that hue ({q})")
    except Exception as error:   # noqa: BLE001
        check(False, f"skin_flat works ({type(error).__name__}: {error})")
    # B8G8R8A8 rules (like the altar separators): supported by both recipes
    raw = bytearray(148 + 4 * 4)
    raw[:4], raw[84:88] = b"DDS ", b"DX10"
    struct.pack_into("<II", raw, 12, 1, 4)
    struct.pack_into("<I", raw, 128, 87)
    for i in range(4):
        raw[148 + 4 * i:152 + 4 * i] = bytes((46, 54, 75, 135))
    for recipe in (RULE, "grey_scale:0.45"):
        check(pipe.format_supported(recipe.partition(":")[0], bytes(raw)), f"{recipe}: B8G8R8A8 supported")
        try:
            done = pipe.recipe(recipe)[2](Path(temp / "bgra.dds"), {"Skin.ornament": [0.45, 0.45, 0.45, 1.0]}, temp, None) \
                if (temp / "bgra.dds").write_bytes(bytes(raw)) else b""
            check(len(done) == len(raw) and done[151] == 135, f"{recipe}: B8G8R8A8 alpha kept ({done[148:152].hex() if done else ''})")
        except Exception as error:   # noqa: BLE001
            check(False, f"{recipe} on B8G8R8A8 ({type(error).__name__}: {error})")

# bp_colour / material_colour: a [Skin] value (four linear numbers) is written as linear, the stock alpha kept
try:
    uexp = bytearray(48000)
    for offset, stock, _ in BLUEPRINTS["WBP_OriginalMenu_InventoryItemDetails"][2]:
        struct.pack_into("<4f", uexp, offset, *stock)
    struct.pack_into("<d", uexp, SHADOW_AT, 1.0)   # RMI-089: the stock UseShadow? literal
    v = {"rowText": [239.0, 239.0, 239.0], "Skin.ornament": [0.45, 0.4, 0.35, 0.2]}
    out = pipe.bp_colour(bytes(uexp), "WBP_OriginalMenu_InventoryItemDetails", v)
    o = struct.unpack_from("<4f", out, 46544)
    check(all(abs(a - b) < 1e-6 for a, b in zip(o, (0.45, 0.4, 0.35, 1.0))), f"bp_colour: Skin value linear, stock alpha kept ({o})")
    t = struct.unpack_from("<4f", out, 46054)
    check(abs(t[0] - pipe.linear(239)) < 1e-6, f"bp_colour: an sRGB [Colours] value still linearised ({t})")
    mic = bytearray(9542)
    struct.pack_into("<4f", mic, 169, 0.86316, 0.3564, 0.13014, 1.0)
    r = struct.unpack_from("<4f", pipe.recipe("material_colour:MIC_UI_SkillCircularProgress")[2](bytes(mic), {"Skin.ring": [0.55, 0.55, 0.55, 1.0]}, None, None), 169)
    check(all(abs(a - b) < 1e-6 for a, b in zip(r, (0.55, 0.55, 0.55, 1.0))), f"material_colour: the ring override ({r})")
    dt = bytearray(730)
    struct.pack_into("<4f", dt, 69, *WARM)
    struct.pack_into("<4f", dt, 425, *WARM)
    d = pipe.bp_colour(bytes(dt), "DT_StatsMenu_SkillDescriptionRichTextStyle_PerLevelDescription",
                       {"rowText": [239.0, 239.0, 239.0], "Skin.emphasisInk": [0.125, 0.12, 0.125, 1.0]})
    check(abs(struct.unpack_from("<f", d, 425)[0] - 0.125) < 1e-6 and abs(struct.unpack_from("<f", d, 69)[0] - pipe.linear(239)) < 1e-6,
          "the perk table: Default = rowText, RT_BlueMed = emphasisInk")
except Exception as error:   # noqa: BLE001
    check(False, f"bp_colour with [Skin] values ({type(error).__name__}: {error})")

# ---- RMI-089: the Inventory details page drop shadow (brown strip along the list panel) is baked off at its writer ----
# KB docs/reverse-engineering/dark-mode-ui.md details-view lookups: Construct() sets UseShadow? = SelectFloat(0.0, 1.0,
# IsInContainer); the 1.0 (Inventory) is an EX_DoubleConst at .uexp byte 11509 (build 1.512.105.0). Custom only (MRCustom_P).
lit = menus.get("inventory", {}).get("blueprint_literals", {}).get("WBP_OriginalMenu_InventoryItemDetails", [])
check(len(lit) == 1 and lit[0].get("offset") == SHADOW_AT and lit[0].get("type") == "double" and lit[0].get("stock") == 1.0
      and lit[0].get("value") == 0.0, f"inventory.json blueprint_literals: UseShadow? 1.0 -> 0.0 at {SHADOW_AT} ({lit})")
try:
    out = pipe.bp_colour(bytes(uexp), "WBP_OriginalMenu_InventoryItemDetails", v)
    check(struct.unpack_from("<d", out, SHADOW_AT)[0] == 0.0, "bp_colour: UseShadow? literal baked to 0.0")
    check(out[SHADOW_AT - 9:SHADOW_AT - 1] == uexp[SHADOW_AT - 9:SHADOW_AT - 1], "bp_colour: the Container 0.0 literal untouched")
    moved = bytearray(uexp)
    struct.pack_into("<d", moved, SHADOW_AT, 0.5)
    try:
        pipe.bp_colour(bytes(moved), "WBP_OriginalMenu_InventoryItemDetails", v)
        check(False, "bp_colour: an unexpected UseShadow? stock value is refused")
    except pipe.SkipAsset:
        check(True, "bp_colour: an unexpected UseShadow? stock value is refused")
    if "WBP_OriginalMenu_InventoryItemDetails" in pipe.LAYOUT:
        _, lay = pipe.layout_package(b"", bytes(uexp), "WBP_OriginalMenu_InventoryItemDetails", pipe.custom_colours.SIZE_DEFAULTS)
        check(struct.unpack_from("<d", lay, SHADOW_AT)[0] == 1.0, "the layout pack keeps the stock shadow")
except Exception as error:   # noqa: BLE001
    check(False, f"RMI-089 shadow literal ({type(error).__name__}: {error})")

# ---- 4. the run-time details passes are gone ----
skin = (root / "src/sorting/MenuSkin.cpp").read_text(encoding="utf-8")
skin_h = (root / "src/sorting/MenuSkin.h").read_text(encoding="utf-8")
headers = (root / "src/sorting/InventoryHeaders.cpp").read_text(encoding="utf-8")
tick = (root / "src/sorting/PickerTick.cpp").read_text(encoding="utf-8")
theme = (root / "src/core/UiTheme.h").read_text(encoding="utf-8")
for name in ("StyleDetails", "StyleLevelDescription", "StyleListEntries", "OnDetailsUpdated", "OnEffectLineEvent", "WatchDetails",
             "RefreshButtonRules", "g_unhoveredRules", "RowSeparator.png", "details image", "details event"):
    check(name not in skin, f"MenuSkin.cpp: {name} removed")
for name in ("WatchDetails", "RefreshButtonRules"):
    check(name not in skin_h, f"MenuSkin.h: {name} removed")
check("WatchDetails" not in headers, "InventoryHeaders.cpp: no details watch")
check("RefreshButtonRules" not in tick, "PickerTick.cpp: no button rule refresh")
check("InkRich(" not in skin and "StyleMagicRow" not in skin, "C.2: the Magic page/row passes are baked (no run time)")
art = theme[theme.find("kDarkArt[]"):theme.find("};", theme.find("kDarkArt[]"))]
for stock in ("T_Property_1_magnitude", "T_Property_1_Range", "T_Property_1_time", "T_Property_1_cost", "T_inv_details_effects_title_BG",
              "T_SigilStone_SubNameBG", "T_Category_Armor", "T_Category_Weapons", "T_CircularProgress_bg", "T_Arrow_Empty",
              "T_Button_State_Hovered_LeftLineFade"):
    check(f'"{stock}"' not in art, f"kDarkArt: no {stock} swap (baked)")
check("kDarkArt" not in theme, "C.2: no kDarkArt (the row icons are baked; the Magic page too, RMI-091)")

print(f"{count} checks, {len(fails)} failures")
for why in fails:
    print("FAIL:", why)
sys.exit(1 if fails else 0)
