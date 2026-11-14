"""Custom accent colours (owner, 2026-11-14, decided on labelled mock-ups; KB mods/menus-revamped/session-log.md):
blue #3DD4FA for every menu title and heading (menuTitle) and the highlighted Magic values (accentValue), gold #E8BB66 for the
effect type/label (effectLabel) and the effect-line icons (effectIcon), browner gold #B98A45 for the effect amounts
(effectValue). New Custom colour elements (owner value requests become Dark builder defaults), grouped in two roles; rowText
keeps the body text. The hourglass goes gold through its stored AddedEffects tint (19908); the magnitude icon has no stored
tint, so its shared texture T_Property_1_magnitude bakes flat effectIcon (owner accepted it is gold in every menu)."""
import re
import struct
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
import custom_colours as cc  # noqa: E402
from pack_builder import pipeline as pipe  # noqa: E402

BLUE, GOLD, BROWN = [61.0, 212.0, 250.0], [232.0, 187.0, 102.0], [185.0, 138.0, 69.0]
ELEMENTS = {"menuTitle": BLUE, "accentValue": BLUE, "effectLabel": GOLD, "effectIcon": GOLD, "effectValue": BROWN}

ini = (ROOT / "config/CustomColours.ini").read_text(encoding="utf-8")
style = (ROOT / "src/core/DarkStyle.h").read_text(encoding="utf-8")
entries = (ROOT / "src/core/rules/CustomColours.h").read_text(encoding="utf-8")
for name, rgb in ELEMENTS.items():
    check(cc.COLOURS.get(name) == rgb, f"custom_colours.COLOURS[{name}] = {rgb}")
    check(name in cc.HELP, f"custom_colours.HELP explains {name}")
    r, g, b = (int(v) for v in rgb)
    check(re.search(rf"^{name}\s*=\s*{r},\s*{g},\s*{b}\b", ini, re.M) is not None, f"config/CustomColours.ini has {name} = {r}, {g}, {b}")
    check(re.search(rf"Rgb {name}\{{ {r}, {g}, {b} \}};", style) is not None, f"DarkStyle.h has Rgb {name}{{ {r}, {g}, {b} }}")
    check(f"MR_CC_RGB({name}," in entries, f"CustomColours.h reads {name}")

roles = {k: v["members"] for k, v in cc.ROLES.items()}
check(roles.get("accentText") == ["menuTitle", "accentValue"], "role accentText = [menuTitle, accentValue]")
check(roles.get("effectGold") == ["effectLabel", "effectIcon", "effectValue"], "role effectGold = [effectLabel, effectIcon, effectValue]")
check(all("rowText" in m for k, m in roles.items() if k == "bodyText"), "rowText stays in bodyText")

MD, AE = "WBP_ModernMenu_MagicDetails", "WBP_LegacyMenu_AddedEffects"
WANT = [
    ("WBP_ModernMenu_MagicMenu", 74024, "menuTitle"), ("WBP_ModernMenu_MagicMenu", 73936, "accentValue"),
    ("WBP_ModernMenu_MagicEntry", 6273, "accentValue"),
    (MD, 18693, "menuTitle"), (MD, 18982, "menuTitle"), (MD, 19374, "menuTitle"), (MD, 19592, "menuTitle"),
    (MD, 18875, "effectLabel"), (MD, 18806, "accentValue"),
    (AE, 18478, "effectLabel"), (AE, 18554, "effectValue"), (AE, 18251, "effectValue"), (AE, 19908, "effectIcon"),
    ("WBP_ModernMenu_ContainerInventory_MainContent", 21276, "menuTitle"),
    ("WBP_ModernMenu_EnchantmentMenu", 73395, "menuTitle"), ("WBP_ModernMenu_EnchantmentMenu", 73289, "menuTitle"),
    ("WBP_ModernMenu_SpellMakingMenu", 56204, "menuTitle"), ("WBP_ModernMenu_SpellMakingMenu", 55826, "menuTitle"),
    ("WBP_ModernMenu_AlchemyMenu", 67439, "menuTitle"), ("WBP_ModernMenu_AlchemyFilters", 14356, "menuTitle"),
    ("WBP_Modern_QuestListPanel", 13743, "menuTitle"),
    ("WBP_ModernMenu_StatsPage2", 8983, "menuTitle"), ("WBP_ModernMenu_StatsPage4", 2885, "menuTitle"),
    ("WBP_ModernMenu_StatsPage5", 8325, "menuTitle"),
]
STAY = [(MD, 19064, "rowText"), (AE, 18320, "rowText")]   # Magic_Active_School, Effect_Name
for package, offset, element in WANT + STAY:
    got = [c for o, _, c in pipe.BLUEPRINT_COLOURS.get(package, []) if o == offset]
    check(got == [element], f"{package} @{offset} bakes {element} (got {got})")

# the new packages are declared in the pack (bp_colour recipe on their asset)
for package, folder in (("WBP_Modern_QuestListPanel", "UI/Original/GameMenuLayer/Quest/"),
                        ("WBP_ModernMenu_StatsPage2", "UI/Modern/GameMenuLayer/Stats/Page2_Attributes/"),
                        ("WBP_ModernMenu_StatsPage4", "UI/Modern/GameMenuLayer/Stats/Page4_Factions/"),
                        ("WBP_ModernMenu_StatsPage5", "UI/Modern/GameMenuLayer/Stats/Page5_Details/")):
    check(pipe.TEXTURES.get(folder + package) == f"bp_colour:{package}", f"{package} is a bp_colour asset in the pack")

ICONS = "Art/UI/Icons/Icon_Stats/"
check(pipe.TEXTURES.get(ICONS + "T_Property_1_magnitude") == "flat:effectIcon",
      "T_Property_1_magnitude (no stored tint in AddedEffects) bakes flat effectIcon (shared: owner accepted)")
check(pipe.TEXTURES.get(ICONS + "T_Property_1_time") == "flat:headerIcon",
      "T_Property_1_time stays flat headerIcon (the hourglass goes gold through its tint)")

# the hourglass tint lands on the gold: tint x headerIcon texture == effectIcon (sRGB within 2)
values = dict(cc.DEFAULTS)
uexp = bytearray(20100)
for offset, stock, _ in pipe.BLUEPRINT_COLOURS.get(AE, []):
    struct.pack_into("<4f", uexp, offset, *stock)
try:
    out = pipe.bp_colour(bytes(uexp), AE, values)
    tint = struct.unpack_from("<4f", out, 19908)
    tex = pipe.linear(values["headerIcon"][0])
    srgb = [255 * (t * tex * 12.92 if t * tex <= 0.0031308 else 1.055 * (t * tex) ** (1 / 2.4) - 0.055) for t in tint[:3]]
    check(all(abs(a - b) <= 2.0 for a, b in zip(srgb, GOLD)) and tint[3] == 1.0,
          f"the hourglass shows effectIcon gold through its tint (got sRGB {[round(v) for v in srgb]})")
except Exception as error:   # noqa: BLE001
    check(False, f"bp_colour patches the AddedEffects colours ({error})")

print(f"accent_colours_tests: {count} checks, {len(fails)} failures")
for why in fails:
    print("  FAIL:", why)
sys.exit(1 if fails else 0)
