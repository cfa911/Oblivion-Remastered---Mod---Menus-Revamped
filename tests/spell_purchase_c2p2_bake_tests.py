"""Magic Step C.2 part 2, Spell Purchase colours (owner 2026-11-14, annotated screenshot + answers; KB
mods/menus-revamped/menus/SpellPurchase/CHECKLIST.md C2P2-1..4):
- C2P2-1: the merchant/player block flourish (WBP_ModernMenu_SpellPurchase `Separator`, white art T_InventorySeparator,
  tint at .uexp 28251) gold effectLabel. Spell Purchase only (the tint is in its own package).
- C2P2-2: the details Range and Cost values (WBP_ModernMenu_MagicDetails Magic_Castable_Range_Value 19781,
  Magic_Castable_Cost_Value 19304) title blue accentValue; shared with Magic (same details view).
- C2P2-3: the effect line's area value (WBP_LegacyMenu_AddedEffects AreaText 18182) gold effectValue like the magnitude and
  duration; shared with every effect list (Inventory, Magic, Sigil Stone, Enchanting, Spellmaking, Alchemy).
- C2P2-4: the row names (WBP_ModernMenu_SpellPurchaseItem effect_text 2920) rowText like the Magic row names."""
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
alchemy = json.loads((ROOT / "tools/menus/alchemy.json").read_text(encoding="utf-8"))


def row(data, package, offset):
    return next((r for r in data["blueprint_colours"].get(package, []) if r["offset"] == offset), None)


def asset(data, package):
    return next((a for a in data["assets"] if a["recipe"] == "bp_colour:" + package), None)


def is_colour(data, package, offset, colour, stock, why):
    r = row(data, package, offset)
    check(r is not None and r["colour"] == colour and r["stock"] == stock, why)


def keys_match(data, package, why):
    a = asset(data, package)
    want = sorted({r["colour"] for r in data["blueprint_colours"].get(package, [])})
    check(a is not None and a["colours"] == want, why + f" (asset row lists exactly {want})")


SEP = [0.1608, 0.1098, 0.0784, 1.0]
is_colour(magic, "WBP_ModernMenu_SpellPurchase", 28251, "effectLabel", SEP, "C2P2-1: the Spell Purchase flourish tint is gold")
keys_match(magic, "WBP_ModernMenu_SpellPurchase", "C2P2-1: WBP_ModernMenu_SpellPurchase has a bp_colour asset row")
a = asset(magic, "WBP_ModernMenu_SpellPurchase")
check(a is not None and a["asset"] == "UI/Modern/GameMenuLayer/SpellPurchase/WBP_ModernMenu_SpellPurchase",
      "C2P2-1: the asset path is the Spell Purchase menu")

DARK = [0.0999, 0.0561, 0.0395, 1.0]
is_colour(magic, "WBP_ModernMenu_MagicDetails", 19781, "accentValue", DARK, "C2P2-2: Magic_Castable_Range_Value is title blue")
is_colour(magic, "WBP_ModernMenu_MagicDetails", 19304, "accentValue", DARK, "C2P2-2: Magic_Castable_Cost_Value is title blue")
keys_match(magic, "WBP_ModernMenu_MagicDetails", "C2P2-2: MagicDetails keys")

AREA = [0.0723, 0.0382, 0.0284, 1.0]
is_colour(alchemy, "WBP_LegacyMenu_AddedEffects", 18182, "effectValue", AREA, "C2P2-3: AreaText is gold like the other values")
keys_match(alchemy, "WBP_LegacyMenu_AddedEffects", "C2P2-3: AddedEffects keys")
# owner (preview answer 2026-11-14): "you also should bake the icon": the AreaIcon tint (T_Property_1_Range, headerIcon art)
# gold like the hourglass (same x1.031 offset for the headerIcon art); the shared texture itself stays white elsewhere.
r = row(alchemy, "WBP_LegacyMenu_AddedEffects", 19841)
check(r is not None and r["colour"] == "effectIcon" and r["stock"] == [0.7875, 0.7875, 0.7875, 1.0] and r.get("scale") == 1.031,
      "C2P2-3: the area icon tint (19841) is effectIcon gold, scale 1.031 like the hourglass")

is_colour(magic, "WBP_ModernMenu_SpellPurchaseItem", 2920, "rowText", DARK, "C2P2-4: the row name is rowText like Magic rows")
keys_match(magic, "WBP_ModernMenu_SpellPurchaseItem", "C2P2-4: WBP_ModernMenu_SpellPurchaseItem has a bp_colour asset row")
a = asset(magic, "WBP_ModernMenu_SpellPurchaseItem")
check(a is not None and a["asset"] == "UI/Modern/GameMenuLayer/SpellPurchase/WBP_ModernMenu_SpellPurchaseItem",
      "C2P2-4: the asset path is the Spell Purchase row")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
