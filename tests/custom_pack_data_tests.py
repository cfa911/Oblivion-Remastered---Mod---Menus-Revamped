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
MENUS = ("inventory", "magic", "alchemy")   # build order: the order of today's single table
manifest = json.loads((root / "assets/CustomPack/MRCustom_P.json").read_text(encoding="utf-8"))
shipped = manifest["textures"]

data = {}
for menu in MENUS:
    path = menus_dir / f"{menu}.json"
    check(path.is_file(), f"tools/menus/{menu}.json exists")
    if path.is_file():
        try:
            data[menu] = json.loads(path.read_text(encoding="utf-8"))
        except ValueError as error:
            check(False, f"tools/menus/{menu}.json is valid JSON ({error})")
check(sorted(p.stem for p in menus_dir.glob("*.json")) == sorted(MENUS) if menus_dir.is_dir() else False,
      "tools/menus holds exactly the inventory, magic and alchemy data files")

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
check(len(assets) == 69, f"the data files describe 69 assets (found {len(assets)})")
check(len(set(assets)) == len(assets), "no asset is listed twice across the data files")
check(set(assets) == set(shipped), "the data files list exactly the assets of the shipped MRCustom_P")
check(assets == list(shipped), "the data files keep today's build order (inventory, magic, alchemy)")
for row in rows:
    asset = row.get("asset")
    if asset in shipped:
        check(row.get("recipe") == shipped[asset]["recipe"], f"{asset}: same recipe as the shipped pack")
        check(row.get("colours") == shipped[asset]["style"], f"{asset}: colour elements match the shipped manifest")

inventory, magic, alchemy = (data.get(m, {}) for m in MENUS)
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
                             "WBP_ModernPrefab_EditableText", "WBP_LegacyMenu_AddedEffects"]),
      "alchemy.json holds the five blueprint colour packages")
check(all(isinstance(e.get("offset"), int) and len(e.get("stock", [])) == 4 and isinstance(e.get("colour"), str)
          for entries in bps.values() for e in entries) and bps,
      "each blueprint colour has its stock check (offset + expected float4) and a colour element")
for row in rows:
    if row.get("recipe", "").startswith("bp_colour:"):
        package = row["recipe"].split(":", 1)[1]
        check(package in bps, f"{package}: its blueprint colours are in a data file")

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
