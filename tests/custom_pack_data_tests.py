"""Custom pack builder Phase 1 (owner 2026-11-11): the pack's asset tables live in one data file per menu (tools/menus/<menu>.json),
not in tools/make_dark_pack.py. The recipes stay code. The data must describe exactly the pack that is shipped today."""
import importlib.util
import json
import re
import sys

import source_paths

fails = []
count = 0


def check(ok, why):
    global count
    count += 1
    if not ok:
        fails.append(why)


root = source_paths.ROOT
menus_dir = root / "tools/menus"
MENUS = ("inventory", "magic", "alchemy", "common", "enchanting", "spellmaking", "tabs")   # build order ("order" in each file); Phase 7 added enchanting and spellmaking, RMI-097 tabs
manifest = json.loads((root / "assets/CustomPack/MRCustom_P.json").read_text(encoding="utf-8"))
shipped = {k: v for k, v in manifest["textures"].items() if v.get("recipe") != "layout"}   # decision 46: the layout packages ride along (layout.json)

data = {}
for menu in MENUS:
    path = menus_dir / f"{menu}.json"
    check(path.is_file(), f"tools/menus/{menu}.json exists")
    if path.is_file():
        try:
            data[menu] = json.loads(path.read_text(encoding="utf-8"))
        except ValueError as error:
            check(False, f"tools/menus/{menu}.json is valid JSON ({error})")
check(sorted(p.stem for p in menus_dir.glob("*.json")) == sorted(MENUS + ("layout",)) if menus_dir.is_dir() else False,
      "tools/menus holds exactly the menu colour data files and the layout sizes (layout.json, Step B)")

rows = []
for menu in MENUS:
    entry = data.get(menu, {})
    check(isinstance(entry.get("menu"), str) and entry.get("menu"), f"{menu}.json names its menu")
    assets = entry.get("assets", [])
    check(isinstance(assets, list) and assets, f"{menu}.json lists its assets")
    for row in assets:
        rows.append(row)
        name = row.get("asset", "?")
        check(isinstance(row.get("recipe"), str) and row["recipe"], f"{name}: has a recipe")
        check(isinstance(row.get("colours"), list), f"{name}: lists the colour element(s) it reads")
        check(isinstance(row.get("shared_with"), list), f"{name}: has a shared-with list (empty when the menu owns it alone)")

assets = [row.get("asset") for row in rows]
check(len(assets) == 218, f"the data files describe 218 assets (C.3 Alchemy 2026-11-14: + the two filter tab decorators, the row outline and the leaf strip; accent colours 2026-11-14: + Quests/Character title blueprints x4; C.2: + 11 _ListSmall icons, WBP_ModernMenu_MagicMenu, WBP_ModernMenu_MagicEntry, DT_UI_Magic_SpellEffectiveness_RichTextStyle; RMI-091: + T_MagicBackground, T_Trading_Title_Background, T_Line_glow, T_Line_glow_long, WBP_ModernMenu_Container; RMI-097: the 13 Inventory/Magic/Enchanting tab rows moved to tabs.json, + 25 more tab textures; RMI-096: popup paper T_UI_BackgroundHorizontal + 15 popup text packages + 4 laurel/slider textures; 2026-11-13: + icon count, rich-text default, stolen mark, Empty band; Phase 7: 69 + 2 scrollbar materials + 4 shared icons + 13 Enchanting + 2 Spellmaking; RMI-087: 8 Enchanting + 5 Spellmaking + 4 shared button; 28a: 3 shared + 5 Inventory + 5 Alchemy; 28b: 18 Inventory + 3 Magic + 1 Alchemy; found {len(assets)})")
check(len(set(assets)) == len(assets), "no asset is listed twice across the data files")
check(set(assets) == set(shipped), "the data files list exactly the assets of the shipped MRCustom_P")
check(assets == list(shipped), "the data files keep the build order (inventory, magic, alchemy, common, enchanting, spellmaking, tabs)")
for row in rows:
    asset = row.get("asset")
    if asset in shipped:
        check(row.get("recipe") == shipped[asset]["recipe"], f"{asset}: same recipe as the shipped pack")
        check(row.get("colours") == shipped[asset]["style"], f"{asset}: colour elements match the shipped manifest")

inventory, magic, alchemy = (data.get(m, {}) for m in MENUS[:3])
check(inventory.get("glyph_masks", {}).get("tab") ==
      ["T_Category_Weapons", "T_Category_Armor", "T_Category_Alchemy", "T_Category_Misc", "T_Category_All_items"],
      "inventory.json holds the Inventory tab glyph masks in tab order")
check(magic.get("glyph_masks", {}).get("magic_tab") ==
      ["T_Category_OnSelf", "T_Category_Touch", "T_Category_Target", "T_Category_All", "T_Category_ActiveEffects"],
      "magic.json holds the Magic tab glyph masks in tab order")
rich = magic.get("rich_rows", {}).get("DT_UI_MagicMenu", {})
check(rich.get("Default") == {"offset": 69, "stock": [0.0723, 0.0382, 0.0284, 1.0]}
      and rich.get("RT_BlueMed") == {"offset": 425, "stock": [0.0513, 0.0648, 0.1441, 1.0]},
      "magic.json keeps the rich-text row stock checks (offset + expected float4)")
bps = alchemy.get("blueprint_colours", {})
check(sorted(bps) == sorted(["WBP_ModernMenu_FilterEffectEntry", "WBP_ModernPrefab_SelectableButton", "WBP_ModernMenu_ItemEffect",
                             "WBP_ModernPrefab_EditableText", "WBP_LegacyMenu_AddedEffects", "WBP_ModernMenu_AlchemyMenu",
                             "WBP_ModernMenu_AlchemyFilters", "WBP_ModernMenu_Alchemy_FilterEffectButton", "WBP_ModernMenu_UstensilEntry",
                             "WBP_ModernMenu_AlchemyItemEntry"]),
      "alchemy.json holds the ten blueprint colour packages (28a: the Alchemy texts the run-time ink pass coloured)")
check(all(isinstance(e.get("offset"), int) and len(e.get("stock", [])) == 4 and isinstance(e.get("colour"), str)
          for entries in bps.values() for e in entries) and bps,
      "each blueprint colour has its stock check (offset + expected float4) and a colour element")
all_bps = {p for m in data.values() for p in m.get("blueprint_colours", {})}   # Phase 7: enchanting.json too
for row in rows:
    if row.get("recipe", "").startswith("bp_colour:"):
        package = row["recipe"].split(":", 1)[1]
        check(package in all_bps, f"{package}: its blueprint colours are in a data file")

# make_dark_pack.py reads the data files and keeps no asset table of its own
tool = root / "tools/make_dark_pack.py"
tool_text = tool.read_text(encoding="utf-8")
check(not re.search(r'"(?:Art|UI)/[^"]+"\s*:', tool_text), "make_dark_pack.py holds no asset table (they live in tools/menus)")
check("T_Category_" not in tool_text, "make_dark_pack.py holds no glyph mask list")
spec = importlib.util.spec_from_file_location("make_dark_pack", tool)
mdp = importlib.util.module_from_spec(spec)
try:
    spec.loader.exec_module(mdp)
except Exception as error:   # noqa: BLE001
    mdp = None
    check(False, f"make_dark_pack.py imports ({error})")
if mdp is not None:
    check(list(getattr(mdp, "TEXTURES", {}).items()) == [(r.get("asset"), r.get("recipe")) for r in rows],
          "make_dark_pack.TEXTURES is the data files' table, in order")
    check(getattr(mdp, "TAB_MASKS", None) == tuple(inventory.get("glyph_masks", {}).get("tab", ()))
          and getattr(mdp, "MAGIC_TAB_MASKS", None) == tuple(magic.get("glyph_masks", {}).get("magic_tab", ())),
          "make_dark_pack's glyph masks come from the data files")
    check(getattr(mdp, "RICH_ROWS", {}).get("DT_UI_MagicMenu", {}).get("RT_BlueMed") == (425, (0.0513, 0.0648, 0.1441, 1.0)),
          "make_dark_pack.RICH_ROWS comes from magic.json as (offset, stock tuple)")
    recipe = getattr(mdp, "recipe", None)
    for row in rows:
        if recipe and row.get("recipe"):
            floats, rgbs, _ = recipe(row["recipe"])
            check(sorted(set(floats) | set(rgbs)) == row.get("colours"),
                  f"{row.get('asset')}: the data file's colours are the ones its recipe reads")
    loader = getattr(mdp, "load_menus", None)
    check(loader is not None, "make_dark_pack.py has load_menus(folder)")
    if loader:
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory() as temp:
            bad = Path(temp)
            (bad / "a.json").write_text(json.dumps({"menu": "A", "assets": [{"asset": "Art/X", "recipe": "gray", "colours": [], "shared_with": []}]}))
            (bad / "b.json").write_text(json.dumps({"menu": "B", "assets": [{"asset": "Art/X", "recipe": "gray", "colours": [], "shared_with": []}]}))
            try:
                loader(bad)
                check(False, "load_menus refuses an asset listed in two data files")
            except SystemExit:
                check(True, "")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
