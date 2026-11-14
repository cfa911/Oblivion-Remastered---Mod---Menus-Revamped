"""Alchemy Step C.3 (owner 2026-11-14, labelled previews approved; KB mods/menus-revamped/menus/Alchemy/CHECKLIST.md
C.3-1..3, evidence c3-alchemy-gold-tabs-preview.png, c3-alchemy-filter-rows-darker-options.png,
c3-alchemy-filter-outline-preview.png):
- C.3-1: the filter tab decorations gold. T_Alchemy_NegativeEffect_Decorator (the dash; our run-time tab strip copies its
  brush for the dashes and the three-stroke All chevron) and T_Alchemy_PositiveEffect_Decorator (the stock three-line arrow)
  baked flat effectLabel, stock alpha kept. Both Alchemy only.
- C.3-2: the filter row background filterRow 66 -> 40 (owner option B; also the Must Contain button).
- C.3-3: the 1-pixel row outline T_Alchemy_Filters_Overlay (every filter row and the Must Contain button; Alchemy only)
  baked flat in a new key filterOutline, default the accent blue 66, 99, 153 (owner: "softer blue")."""
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


ROOT = source_paths.ROOT
sys.path.insert(0, str(ROOT / "tools"))
import custom_colours  # noqa: E402

data = json.loads((ROOT / "tools/menus/alchemy.json").read_text(encoding="utf-8"))
rows = {a["asset"]: a for a in data["assets"]}
ALC = "Art/UI/Modern/GameMenuLayer/Alchemy/"
for name in ("T_Alchemy_NegativeEffect_Decorator", "T_Alchemy_PositiveEffect_Decorator"):
    row = rows.get(ALC + name)
    check(row is not None, f"C.3-1: {name} has a row in alchemy.json")
    if row:
        check(row["recipe"] == "flat:effectLabel" and row["colours"] == ["effectLabel"], f"C.3-1: {name} is flat effectLabel gold")
        check(row.get("shared_with") == ["Alchemy only"], f"C.3-1: {name} is marked Alchemy only")
row = rows.get(ALC + "Filters/T_Alchemy_Filters_Overlay")
check(row is not None, "C.3-3: T_Alchemy_Filters_Overlay has a row in alchemy.json")
if row:
    check(row["recipe"] == "flat:filterOutline" and row["colours"] == ["filterOutline"], "C.3-3: the outline is flat filterOutline")
    check(row.get("shared_with") == ["Alchemy only"], "C.3-3: the outline texture is marked Alchemy only")

C = custom_colours.COLOURS
check(C.get("filterRow") == [40.0, 40.0, 40.0], "C.3-2: the filterRow default is 40, 40, 40")
check(C.get("filterOutline") == [66.0, 99.0, 153.0], "C.3-3: the filterOutline default is the accent blue 66, 99, 153")
keys = list(C)
check("filterOutline" in keys and keys.index("filterOutline") == keys.index("filterMark") + 1,
      "C.3-3: filterOutline follows filterMark in the default-file order")
check(bool(custom_colours.HELP.get("filterOutline")), "C.3-3: filterOutline has a builder label")
check(any("filterOutline" in r["members"] for r in custom_colours.ROLES.values()), "C.3-3: filterOutline belongs to a builder role")

dark = (ROOT / "src/core/DarkStyle.h").read_text(encoding="utf-8-sig")
check("Rgb filterRow{ 40, 40, 40 };" in dark, "C.3-2: DarkStyle.h filterRow 40")
check("Rgb filterOutline{ 66, 99, 153 };" in dark, "C.3-3: DarkStyle.h names filterOutline")
cc = (ROOT / "src/core/rules/CustomColours.h").read_text(encoding="utf-8-sig")
check(re.search(r"MR_CC_RGB\(filterMark, [^)]*\),\s*MR_CC_RGB\(filterOutline,", cc) is not None,
      "C.3-3: CustomColours.h reads filterOutline right after filterMark")
ini = (ROOT / "config/CustomColours.ini").read_text(encoding="utf-8-sig")
check(re.search(r"^filterRow = 40, 40, 40 ", ini, re.M) is not None, "C.3-2: the shipped colour file has filterRow 40")
check(re.search(r"^filterMark = .*\n^filterOutline = 66, 99, 153 ", ini, re.M) is not None,
      "C.3-3: the shipped colour file has filterOutline after filterMark")

# ---- round 3 (owner 2026-11-14, estimate c3-alchemy-round3-preview.png approved) ----
bp = data["blueprint_colours"]


def has(package, offset, colour, stock):
    return any(r["offset"] == offset and r["colour"] == colour and r["stock"] == stock for r in bp.get(package, []))


DECO = [0.2502, 0.1845, 0.1119, 1.0]
check(has("WBP_ModernMenu_AlchemyFilters", 15133, "effectLabel", DECO) and has("WBP_ModernMenu_AlchemyFilters", 15582, "effectLabel", DECO),
      "C.3-9: the Filters-title flourishes (LeftDeco, RightDeco tints) are gold in Alchemy only")
LINE = [0.3813, 0.2789, 0.1812, 1.0]
check(has("WBP_ModernMenu_AlchemyMenu", 68998, "effectLabel", LINE) and has("WBP_ModernMenu_AlchemyMenu", 69083, "effectLabel", LINE),
      "C.3-11: the apparatus divider (Alc_item_details_line, _1 tints) is gold")
row = rows.get(ALC + "T_Alchemy_Right_BG_pattern")
check(row is not None and row["recipe"] == "flat:pattern" and row["colours"] == ["pattern"] and row.get("shared_with") == ["Alchemy only"],
      "C.3-10: the leaf strip behind the Filters title is baked flat pattern (softer blue), Alchemy only")
check(not any(a.endswith("/T_setting_Deco_short") for a in rows), "C.3-9: the shared flourish texture stays stock (Save/Load, Settings)")
for package in ("WBP_ModernMenu_AlchemyFilters", "WBP_ModernMenu_AlchemyMenu"):
    asset = next((a for a in data["assets"] if a["recipe"] == f"bp_colour:{package}"), None)
    keys = sorted({r["colour"] for r in bp.get(package, [])})
    check(asset is not None and asset["colours"] == keys, f"{package}: the asset row lists exactly its blueprint keys ({keys})")
check(C.get("menuTitle") == [79.0, 124.0, 176.0] and C.get("accentValue") == [79.0, 124.0, 176.0],
      "C.3-13: menuTitle and accentValue default #4f7cb0")
check(abs(custom_colours.SKIN.get("scrollInside", 0) - 0.8632) < 0.0005, "C.3-12: the scrollbar thumb fill defaults white (sRGB 239)")
roles = custom_colours.ROLES
check(any(r["members"] == ["Skin.scrollInside"] for r in roles.values()), "C.3-12: the thumb fill has its own builder role")
check("Skin.scrollInside" not in roles["scrollbars"]["members"], "C.3-12: the Scrollbars role keeps the track and border only")
check("Rgb menuTitle{ 79, 124, 176 };" in dark and "Rgb accentValue{ 79, 124, 176 };" in dark, "C.3-13: DarkStyle.h title blue #4f7cb0")
check("scrollInside = 0.8632f" in dark, "C.3-12: DarkStyle.h scrollbar thumb fill white")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
