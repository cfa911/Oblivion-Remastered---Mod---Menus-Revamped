"""Charcoal tabs (owner recipe and preview approvals, 2026-11-13): every parchment tab texture of the Custom pack is recoloured
pixel by pixel to dark charcoal parchment with light icons (recipe charcoal:<preset>, tools/pack_builder/charcoal_tabs.py),
replacing tab_paper (Inventory, Magic) and tab_strip (Enchanting) and adding the Quest, Stats, page-tab arrow and character
creation tabs. Palette fixed in the recipe (owner: "Fixed in recipe"); no colour-file element is read.
- data: the 38 assets, each with its preset, no colour elements;
- every result keeps the stock size, header and alpha (every pixel); greys are neutral;
- ink inside an icon box -> light, a stroke outside every box -> paper; BC1 strips stay BC1;
- glyph (arrows) -> primary; emblem (Stats icons): brown rim -> light, paper fill -> charcoal;
- pattern (laurels): a faint grey leaf -> lifted toward white, a warm stain of the same darkness -> stays paper.
KB: docs/reverse-engineering/dark-mode-ui.md "Tab atlases", techniques page charcoal-tab-recolour."""
import json
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
import pack_builder.pipeline as pipe  # noqa: E402

from PIL import Image  # noqa: E402

INV = "Art/UI/Modern/GameMenuLayer/Inventory/Tabs/"
MAG = "Art/UI/Modern/GameMenuLayer/magic/Tabs/"
ENC = "Art/UI/Modern/GameMenuLayer/enchantment/Tabs/"
QST = "Art/UI/Modern/GameMenuLayer/Quest/QuestPage/"
COM = "Art/UI/Common/"
SKC = "Art/UI/Modern/GameMenuLayer/SkillAndClass/PaperTabs/"
CHC = "Art/UI/Modern/GameMenuLayer/CharacterCreation/"
ICO = "Art/UI/Icons/Icon_Category/"
EXPECTED = {}
EXPECTED.update({INV + n: "charcoal:inventory" for n in ("T_Tab_weapons", "T_Tab_armor", "T_Tab_alchemy", "T_Tab_misc", "T_Tab_all")})
EXPECTED.update({MAG + n: "charcoal:magic" for n in ("T_Magic_Tabs_Self", "T_Magic_Tabs_Touch", "T_Magic_Tabs_Target",
                                                     "T_Magic_Tabs_All", "T_Magic_Tabs_ActiveEffect")})
EXPECTED.update({ENC + n: "charcoal:enchanting" for n in ("T_Enchantment_Tab_Gear", "T_Enchantment_Tab_SoulGem", "T_Enchantment_Tab_Effect")})
EXPECTED.update({QST + n: "charcoal:quests" for n in ("T_QuestPage_AvailableQuestTabSelected", "T_QuestPage_CompletedQuestTabSelected")})
EXPECTED.update({COM + "Tabs/" + n: "charcoal" for n in ("T_Tab_1", "T_Tab_2", "T_Tab_3", "T_Tab_4", "T_Tab_5")})
EXPECTED.update({COM + n: "charcoal:glyph" for n in ("T_Tab_arrow_on", "T_Tab_arrow_off")})
EXPECTED.update({SKC + n: "charcoal:flourish" for n in ("T_UI_SkillClass_PaperTab_Selected", "T_UI_SkillClass_LeftPaperTab_Active")})
EXPECTED.update({SKC + n: "charcoal:laurel" for n in ("T_UI_SkillsClass_TabLeft_On", "T_UI_SkillsClass_TabMiddle_On", "T_UI_SkillsClass_TabRight_On")})
EXPECTED.update({SKC + n: "charcoal:paper" for n in ("T_UI_SkillClass_PaperTab_Unselected", "T_UI_SkillClass_LeftPaperTab_Inactive",
                                                     "T_UI_SkillsClass_TabLeft_Off", "T_UI_SkillsClass_TabMiddle_Off")})
EXPECTED.update({CHC + "T_Character_BG_Tab_L": "charcoal:header_l", CHC + "T_Character_BG_Tab_R": "charcoal:header_r"})
EXPECTED.update({ICO + n: "charcoal:emblem" for n in ("T_Category_attributes", "T_Category_def", "T_Category_details",
                                                      "T_Category_factions", "T_Category_skills")})
check(len(EXPECTED) == 38, f"38 tab textures in scope ({len(EXPECTED)})")

# ---- 1. data rows ----
rows = {}
for path in sorted((root / "tools/menus").glob("*.json")):
    for row in json.loads(path.read_text(encoding="utf-8")).get("assets", []):
        rows[row["asset"]] = row
for asset, preset in EXPECTED.items():
    row = rows.get(asset, {})
    check(row.get("recipe") == preset, f"{Path(asset).name}: recipe {preset} ({row.get('recipe')})")
    check(row.get("colours", None) == [], f"{Path(asset).name}: reads no colour element (palette fixed; {row.get('colours')})")
check(not any(r.get("recipe") in ("tab_paper", "tab_strip") or str(r.get("recipe", "")).startswith(("tab:", "magic_tab:"))
              for r in rows.values()), "no data row uses the old tab recipes")
check("custom_pack_charcoal_tabs_tests" in (root / "tests/run_suites.py").read_text(encoding="utf-8"),
      "the custom-pack group runs this suite")

# ---- 2. the recipe table ----
presets = sorted(set(EXPECTED.values()))
fns = {}
for name in presets:
    try:
        floats, rgbs, fn = pipe.recipe(name)
        check(floats == () and rgbs == (), f"{name} reads no colour element ({floats}, {rgbs})")
        fns[name] = fn
    except Exception as error:   # noqa: BLE001
        check(False, f"pipeline.recipe('{name}') exists ({type(error).__name__}: {error})")
try:
    from pack_builder import charcoal_tabs as ct
    want = {"black": (11, 11, 14), "charcoal": (23, 23, 25), "light": (41, 41, 45), "primary": (242, 240, 235),
            "secondary": (216, 213, 208), "inactive": (133, 133, 137), "faded": (85, 85, 91)}
    check(ct.PALETTE == want, f"the owner palette is the recipe's ({ct.PALETTE})")
except Exception as error:   # noqa: BLE001
    check(False, f"charcoal_tabs imports ({type(error).__name__}: {error})")


# ---- 3. the retired tab keys (owner: "they stay in the file, marked as no effect") ----
RETIRED = ("tabPaperDark", "tabPaperLight", "tabGlyph", "enchantingTabPaper", "enchantingTabIcon")
try:
    import custom_colours  # noqa: E402
    from pack_builder import model  # noqa: E402
    rendered = custom_colours.render(custom_colours.DEFAULTS)
    reader = (root / "src/core/rules/CustomColours.h").read_text(encoding="utf-8")
    for key in RETIRED:
        check(key in custom_colours.COLOURS, f"{key} stays in the colour file")
        check("no effect" in custom_colours.HELP.get(key, ""), f"{key}: its label says no effect ({custom_colours.HELP.get(key)})")
        check(not any(key in r["members"] for r in custom_colours.ROLES.values()), f"{key}: no role feeds it any more")
        check(f'MR_CC_RGB({key}, "{custom_colours.HELP.get(key)}")' in reader, f"{key}: the plugin's default file has the same label")
    header = [line for line in rendered.splitlines() if "retained with no effect" in line and "[Tab]" in line]
    check(len(header) == 1 and all(k in header[0] for k in RETIRED), f"the colour file header names the retired tab keys ({header})")
    check(header and header[0] in reader, "the plugin's default file has the same header line")
    check((root / "config/CustomColours.ini").read_text(encoding="utf-8") == rendered, "config/CustomColours.ini is the rendered default file")
    with tempfile.TemporaryDirectory() as temp:
        ini = Path(temp) / "c.ini"
        ini.write_text(rendered, encoding="utf-8")
        m = model.ColourModel(ini)
        for key in RETIRED:
            check(m.entry("Colours", key).applies == "Inactive", f"model: {key} Inactive ({m.entry('Colours', key).applies})")
    # an owner file written before RMI-097 still holds [Roles] tabShadow / tabLight: dropped quietly (no "not a known entry"
    # warning in the Pack Builder log), while a really unknown entry is still reported
    import contextlib
    import io
    with tempfile.TemporaryDirectory() as temp:
        ini = Path(temp) / "old.ini"
        ini.write_text("[Roles]\ntabShadow = 17, 17, 17\ntabLight = 30, 30, 30\nnoSuchRole = 1, 2, 3\n", encoding="utf-8")
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            found = custom_colours.read_entries(ini)
        log = err.getvalue()
        check("tabShadow" not in log and "tabLight" not in log, f"retired roles tabShadow / tabLight are dropped quietly ({log!r})")
        check("noSuchRole" in log, f"an unknown entry is still reported ({log!r})")
        check(not any(n in ("tabShadow", "tabLight") for _, n, _ in found), "retired roles are not read")
except Exception as error:   # noqa: BLE001
    check(False, f"the retired tab keys ({type(error).__name__}: {error})")


def run(name, image, fmt, temp):
    """image -> stock DDS (fmt DXT5/DXT1) -> recipe -> (stock image, baked image, stock bytes, baked bytes)"""
    path = temp / f"{name.replace(':', '_')}_{fmt}.dds"
    image.save(path, pixel_format=fmt)
    stock_bytes = path.read_bytes()
    baked_bytes = fns[name](path, {}, temp, None)
    out = temp / ("o_" + path.name)
    out.write_bytes(baked_bytes)
    with Image.open(path) as a, Image.open(out) as b:
        return a.convert("RGBA"), b.convert("RGBA"), stock_bytes, baked_bytes


def same_frame(name, s, b, sb, bb):
    check(len(bb) == len(sb), f"{name}: size kept")
    head = pipe.texture_header(sb)
    check(bb[:head] == sb[:head], f"{name}: header kept")
    check(list(s.getchannel("A").get_flattened_data()) == list(b.getchannel("A").get_flattened_data()), f"{name}: alpha = stock, every pixel")
    spread = max(max(p[:3]) - min(p[:3]) for p in b.get_flattened_data())
    check(spread <= 4, f"{name}: neutral greys (largest channel spread {spread})")


def paper_image(w, h, alpha=lambda x, y: 255):
    im = Image.new("RGBA", (w, h))
    im.putdata([(200 + (x * 7 + y * 3) % 5 - 2, 190 + (x * 3 + y * 5) % 5 - 2, 175, alpha(x, y)) for y in range(h) for x in range(w)])
    return im


def stroke(im, box, rgb):
    px = im.load()
    for y in range(box[1], box[3]):
        for x in range(box[0], box[2]):
            px[x, y] = (*rgb, px[x, y][3])


with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    try:
        # inventory: a brown stroke in the first icon box (134..208, 12..87) and one far outside every box
        im = paper_image(1024, 256, lambda x, y: 255 if y < 200 else max(0, 255 - 4 * (y - 200)))
        stroke(im, (160, 30, 168, 70), (110, 60, 35))
        stroke(im, (990, 30, 998, 70), (110, 60, 35))
        if "charcoal:inventory" in fns:
            s, b, sb, bb = run("charcoal:inventory", im, "DXT5", temp)
            same_frame("charcoal:inventory", s, b, sb, bb)
            inside, outside, paper = b.getpixel((164, 50)), b.getpixel((994, 50)), b.getpixel((600, 150))
            check(min(inside[:3]) >= 120, f"inventory: an icon stroke turns light ({inside[:3]})")
            check(max(outside[:3]) <= 60, f"inventory: a stroke outside every box stays paper ({outside[:3]})")
            check(8 <= paper[0] <= 60, f"inventory: the parchment maps into the charcoal range ({paper[:3]})")
        # enchanting on a BC1 strip: stays BC1, opaque
        im = paper_image(1024, 128)
        stroke(im, (190, 30, 198, 70), (110, 60, 35))
        if "charcoal:enchanting" in fns:
            s, b, sb, bb = run("charcoal:enchanting", im, "DXT1", temp)
            same_frame("charcoal:enchanting (BC1)", s, b, sb, bb)
            check(pipe.bc1_header_size(bb) > 0, "enchanting: a BC1 strip stays BC1")
            check(pipe.format_supported("charcoal", sb), "format_supported: charcoal takes BC1")
            check(min(b.getpixel((194, 50))[:3]) >= 120, f"enchanting: an icon stroke turns light ({b.getpixel((194, 50))[:3]})")
        # glyph: an arrow on transparency -> primary, alpha kept
        im = Image.new("RGBA", (128, 32), (98, 41, 10, 0))
        stroke(im, (10, 8, 30, 24), (98, 41, 10))
        im.putdata([(p[0], p[1], p[2], 200 if 10 <= i % 128 < 30 and 8 <= i // 128 < 24 else 0) for i, p in enumerate(im.get_flattened_data())])
        if "charcoal:glyph" in fns:
            s, b, sb, bb = run("charcoal:glyph", im, "DXT5", temp)
            same_frame("charcoal:glyph", s, b, sb, bb)
            check(min(b.getpixel((20, 16))[:3]) >= 225, f"glyph: the arrow takes the primary colour ({b.getpixel((20, 16))[:3]})")
        # emblem: brown rim around a paper fill on transparency
        im = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
        px = im.load()
        for y in range(40, 216):
            for x in range(40, 216):
                rim = x < 60 or x >= 196 or y < 60 or y >= 196
                px[x, y] = (120, 55, 35, 255) if rim else (150, 140, 130, 255)
        if "charcoal:emblem" in fns:
            s, b, sb, bb = run("charcoal:emblem", im, "DXT5", temp)
            same_frame("charcoal:emblem", s, b, sb, bb)
            check(min(b.getpixel((48, 128))[:3]) >= 180, f"emblem: the brown rim turns light ({b.getpixel((48, 128))[:3]})")
            check(max(b.getpixel((128, 128))[:3]) <= 80, f"emblem: the paper fill turns charcoal ({b.getpixel((128, 128))[:3]})")
        # laurel pattern: a faint grey leaf (6 L* darker, greyer) is lifted; a warm stain of the same darkness is not
        im = paper_image(512, 128)
        stroke(im, (100, 30, 140, 42), (185, 178, 168))   # greyer, darker leaf
        stroke(im, (300, 30, 340, 42), (196, 176, 140))   # warmer stain, about as dark
        if "charcoal:laurel" in fns:
            s, b, sb, bb = run("charcoal:laurel", im, "DXT5", temp)
            same_frame("charcoal:laurel", s, b, sb, bb)
            leaf, stain, paper = b.getpixel((120, 36))[0], b.getpixel((320, 36))[0], b.getpixel((220, 80))[0]
            check(leaf >= paper + 40, f"laurel: a faint grey leaf is lifted toward white (leaf {leaf}, paper {paper})")
            check(stain <= paper + 10, f"laurel: a warm stain stays paper (stain {stain}, paper {paper})")
        # paper-only preset: nothing turns light
        im = paper_image(512, 128)
        stroke(im, (100, 30, 108, 70), (110, 60, 35))
        if "charcoal:paper" in fns:
            s, b, sb, bb = run("charcoal:paper", im, "DXT5", temp)
            same_frame("charcoal:paper", s, b, sb, bb)
            check(max(b.getpixel((104, 50))[:3]) <= 60, f"paper: a brown mark stays paper on a plain tab ({b.getpixel((104, 50))[:3]})")
    except Exception as error:   # noqa: BLE001
        check(False, f"the charcoal recipes run ({type(error).__name__}: {error})")

print(f"{count} checks, {len(fails)} failures")
for why in fails:
    print("FAIL:", why)
sys.exit(1 if fails else 0)
