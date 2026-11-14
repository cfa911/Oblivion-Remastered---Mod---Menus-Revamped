"""Magic's last run-time Custom recolours, baked (Step C.2, owner 2026-11-14: "Bake all in this build"; row icons previewed
and approved: mods/menus-revamped/evidence/c2-row-icons-preview.png in the KB).

What the run-time pass did (src/sorting/MenuSkin.cpp before C.2) and its bake (stock values checked on the game 1.512.105.0
extract; KB mods/menus-revamped/menus/Magic/values.md):
- StyleMagicRow: dark row text -> rowText. WBP_ModernMenu_MagicEntry Magic_entry_CostMagnitude (6273) and Magic_entry_name
  (6344), stock (0.0999, 0.0561, 0.0395).
- StyleRowArt / SwapArt: the 11 T_MagicMenuIcon_*_ListSmall swapped to white-shape PNGs drawn in rowIcon (row image tint
  1,1,1,1: the brown is in the art) -> recipe flat:rowIcon (the same look; shared with Sigil Stone, the effect lists and the
  Inventory enchantment tooltip: owner-approved).
- StyleMagicPage / InkRich: the Spell Effectiveness value and title -> rowText (WBP_ModernMenu_MagicMenu 73936 stock dark,
  73826 stock tan), the three tooltip rich texts that use DT_UI_Magic_SpellEffectiveness_RichTextStyle (73472, 73600, 73713,
  stock tan) and every row of that Magic-only table (69, 425, 781) -> rowText. TEMP-DIAG MRMAGICDARK goes with it."""
import json
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
magic = json.loads((ROOT / "tools/menus/magic.json").read_text(encoding="utf-8"))
colours = magic.get("blueprint_colours", {})
assets = {a["asset"]: a for a in magic.get("assets", [])}
DARK = [0.0723, 0.0382, 0.0284, 1.0]
ROW = [0.0999, 0.0561, 0.0395, 1.0]
TAN = [0.6824, 0.5882, 0.4784, 1.0]
LIGHT = [1.0, 0.9387, 0.8469, 1.0]
BLUE = [0.0513, 0.0648, 0.1441, 1.0]


def has(package, offset, stock, widget=None):
    return any(e.get("offset") == offset and e.get("stock") == stock and e.get("colour") == "rowText"
               and (widget is None or e.get("widget") == widget) for e in colours.get(package, []))


check(has("WBP_ModernMenu_MagicEntry", 6273, ROW, "Magic_entry_CostMagnitude"), "row cost text baked rowText")
check(has("WBP_ModernMenu_MagicEntry", 6344, ROW, "Magic_entry_name"), "row name baked rowText")
check(assets.get("UI/Modern/GameMenuLayer/magic/WBP_ModernMenu_MagicEntry", {}).get("recipe") == "bp_colour:WBP_ModernMenu_MagicEntry",
      "the Magic row blueprint is a Custom colour asset")
for offset, stock, widget in ((73936, DARK, "Magic_SpellEffectivenes_Value"), (73826, TAN, "Magic_Active_SpellEffectiveness_Title"),
                              (73472, TAN, "Magic_Active_SpellEffec_GlobalDetails"), (73600, TAN, "Magic_Active_SpellEffec_HeavyDetails_Text"),
                              (73713, TAN, "Magic_Active_SpellEffec_LightDetails_Text")):
    check(has("WBP_ModernMenu_MagicMenu", offset, stock, widget), f"{widget} ({offset}) baked rowText")
DT = "DT_UI_Magic_SpellEffectiveness_RichTextStyle"
for offset, stock in ((69, LIGHT), (425, BLUE), (781, LIGHT)):
    check(has(DT, offset, stock), f"{DT} row at {offset} baked rowText")
check(assets.get("UI/Modern/GameMenuLayer/magic/" + DT, {}).get("recipe") == "bp_colour:" + DT, "the effectiveness table is a Custom colour asset")
ICONS = ("Ability", "Disease", "Enchantment", "GreaterPower", "LesserPower", "Poison", "Potion", "Scroll", "SpellCustom", "Spell", "WortCraft")
for n in ICONS:
    row = assets.get(f"Art/UI/Modern/GameMenuLayer/magic/SpellType/Small/T_MagicMenuIcon_{n}_ListSmall", {})
    check(row.get("recipe") == "flat:rowIcon" and row.get("colours") == ["rowIcon"], f"T_MagicMenuIcon_{n}_ListSmall baked flat:rowIcon")

# the pipeline loads them
sys.path.insert(0, str(ROOT / "tools"))
from pack_builder import pipeline as pipe  # noqa: E402

check(len(pipe.BLUEPRINT_COLOURS.get("WBP_ModernMenu_MagicMenu", [])) == 6, "MagicMenu: title + 5 effectiveness literals")
check(len(pipe.BLUEPRINT_COLOURS.get(DT, [])) == 3, "the effectiveness table: 3 rows")
check(sum(1 for a in pipe.TEXTURES if a.endswith("_ListSmall")) == 11, "11 row type icons in the texture table")

# no run-time Magic recolour left
skin = (ROOT / "src/sorting/MenuSkin.cpp").read_text(encoding="utf-8-sig") if (ROOT / "src/sorting/MenuSkin.cpp").exists() else ""
skin_h = (ROOT / "src/sorting/MenuSkin.h").read_text(encoding="utf-8-sig") if (ROOT / "src/sorting/MenuSkin.h").exists() else ""
headers = (ROOT / "src/sorting/InventoryHeaders.cpp").read_text(encoding="utf-8-sig")
theme = (ROOT / "src/core/UiTheme.h").read_text(encoding="utf-8-sig")
for name in ("StyleMagicRow", "StyleMagicPage", "OnMagicRowBound", "InkRich", "RebuildEmphasis", "SwapArt", "LightIcon",
             "StyleRowArt", "SkinArt"):
    check(name not in skin + skin_h + headers, f"the run-time pass {name} is gone")
check("MRMAGICDARK" not in skin and "TEMP-DIAG" not in skin, "TEMP-DIAG MRMAGICDARK removed")
check("kDarkArt" not in theme and "_ListSmall" not in theme, "no run-time art swaps left")
check(not list((ROOT / "assets/Skin").glob("MagicRow-*.png")), "the 11 MagicRow PNGs are gone")
check(not (ROOT / "tools/make_dark_row_icons.py").exists(), "their maker script is gone")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
