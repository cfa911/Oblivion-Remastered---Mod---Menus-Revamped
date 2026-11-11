"""The Custom pack's colour file, CustomColours.ini: one value per element (today's DarkStyle.h names), read by the pack recipes
and by the plugin (src/core/rules/CustomColours.h renders the same default file and reads the same keys and ranges).

Usage:
  python tools/custom_colours.py [<output .ini>]   write the default file from src/core/DarkStyle.h (default config/CustomColours.ini)

load(path) returns {element: value}: [r, g, b] (sRGB 0-255) for the [Colours] entries, a number for detailsPage and the [Tab]
greys. A missing file or key gives the built-in default (DEFAULTS, today's Dark values); an unknown key is ignored with a note;
a broken value stops with a message that names the key. [Skin] holds the plugin's run-time tints (linear 0-1, red, green,
blue, alpha; load_skin(path)). The builder also bakes [Skin] pattern. Colours are per element for now; merging them into roles is a later,
owner-reviewed step (KB mods/menus-revamped/evidence/custom-pack-builder-plan.md).
"""
import configparser
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PATH = ROOT / "config/CustomColours.ini"

# today's Dark values (src/core/DarkStyle.h; tests/custom_colours_tests.py keeps them equal)
COLOURS = {
    "panel": [41.0, 41.0, 41.0], "frame": [16.0, 16.0, 16.0], "rowText": [239.0, 239.0, 239.0],
    "rowSeparator": [255.0, 255.0, 255.0], "columnLine": [99.0, 99.0, 99.0], "rowIcon": [239.0, 239.0, 239.0],
    "headerIcon": [247.0, 247.0, 247.0], "pickerIcon": [245.0, 245.0, 245.0], "listHighlight": [91.0, 91.0, 91.0],
    "subTabDisc": [96.0, 96.0, 96.0], "subTabCircle": [72.0, 72.0, 72.0], "subTabIcon": [228.0, 228.0, 228.0],
    "pattern": [150.0, 150.0, 150.0], "enchantedDot": [66.0, 99.0, 153.0], "previewFrameLight": [150.0, 150.0, 150.0],
    "previewFrameDark": [30.0, 30.0, 30.0], "parchmentDark": [8.0, 8.0, 8.0], "parchmentLight": [33.0, 33.0, 33.0],
    "effectBox": [66.0, 99.0, 153.0], "nameBox": [239.0, 239.0, 239.0], "filterRow": [66.0, 66.0, 66.0],
    "filterHover": [112.0, 112.0, 112.0], "filterSelected": [14.0, 22.0, 42.0], "filterMark": [92.0, 140.0, 214.0],
    "alchemyMetal": [56.0, 83.0, 118.0], "alchemyStone": [22.0, 25.0, 31.0],
    "enchantingFrame": [67.0, 75.0, 91.0],
    "spellmakingFrame": [85.0, 130.0, 182.0],
    "altarPaper": [24.0, 25.0, 28.0],
    "enchantingCrystal": [103.0, 159.0, 236.0],
    "enchantingOrnament": [72.0, 78.0, 90.0],
    "spellmakingEye": [86.0, 127.0, 181.0],
    "enchantingTabPaper": [27.0, 27.0, 29.0],
    "enchantingTabIcon": [255.0, 255.0, 255.0],
}
NUMBERS = {"detailsPage": 0.010}   # [Colours] entries that are one number (detailsPage: a linear multiply, 0..1)
TAB = {"selected": 41.0, "unselected": 33.0, "gap": 24.0, "outline": 134.0, "glyph": 242.0, "glyphDim": 162.0}
DEFAULTS = COLOURS | NUMBERS | TAB
# the plugin's run-time tints of the Dark skin (DarkStyle.h SkinTints, header order): four linear values, or one for the scrollbar
SKIN = {
    "rule": [0.65, 0.65, 0.65, 0.55], "guide": [0.55, 0.55, 0.55, 0.13], "ring": [0.55, 0.55, 0.55, 1.0],
    "ornament": [0.45, 0.45, 0.45, 1.0], "pattern": [1.0, 1.0, 1.0, 1.0], "band": [0.41, 0.65, 1.0, 1.0],
    "buttonFill": [0.12, 0.12, 0.12, 1.0], "scrollTrack": 0.0052, "scrollBorder": 0.0052, "scrollInside": 0.0222,
    "emphasisInk": [0.125, 0.12, 0.125, 1.0], "buttonRule": [0.83, 0.83, 0.83, 1.0],
}
# namespaced so the [Colours] base and [Skin] tint stay independent; the scrollbar values are baked into the stock
# scrollbar materials (Phase 7, owner decision 18)
BAKED_SKIN = {"pattern": "Skin.pattern", "scrollTrack": "Skin.scrollTrack", "scrollBorder": "Skin.scrollBorder",
              "scrollInside": "Skin.scrollInside"}
SKIN_HELP = {
    "rule": "Inventory and Magic header horizontal lines",
    "guide": "Inventory and Magic header column guides",
    "ring": "Inventory and Magic details skill level ring",
    "ornament": "Inventory and Magic details decorative dividers",
    "pattern": "Inventory action button laurels - tint and opacity",
    "band": "Inventory and Magic details Effects heading strip (fallback)",
    "buttonFill": "Inventory and Magic details action button fill",
    "scrollTrack": "Scrollbar track in every menu but Settings",
    "scrollBorder": "Scrollbar thumb border in every menu but Settings",
    "scrollInside": "Scrollbar thumb fill in every menu but Settings",
    "emphasisInk": "Inventory and Magic details emphasized perk text",
    "buttonRule": "Inventory and Magic action button divider centres",
}

HELP = {
    "panel": "Inventory and Magic list backgrounds",
    "frame": "Inventory and Magic list edges and shared scrollbar frame",
    "rowText": "Inventory and Magic list and details text",
    "rowSeparator": "Inventory list row divider (inactive)",
    "columnLine": "Inventory and Magic list column dividers",
    "rowIcon": "Magic school, spell type and detail icons",
    "headerIcon": "Inventory and Magic header stat icons",
    "pickerIcon": "Inventory and Magic sort picker icons (inactive)",
    "listHighlight": "Inventory and Magic highlighted row background",
    "subTabDisc": "Inventory selected details tab background",
    "subTabCircle": "Inventory details tab circle",
    "subTabIcon": "Inventory details tab icons",
    "pattern": "Inventory action button laurels - base colour",
    "enchantedDot": "Inventory enchanted item dot",
    "previewFrameLight": "Inventory item preview frame highlights",
    "previewFrameDark": "Inventory item preview frame shadows",
    "parchmentDark": "Alchemy and other parchment menus - shadows",
    "parchmentLight": "Alchemy and other parchment menus - highlights",
    "effectBox": "Alchemy effect boxes",
    "nameBox": "Alchemy and other editable name fields",
    "filterRow": "Alchemy filter row background",
    "filterHover": "Alchemy hovered filter row background",
    "filterSelected": "Alchemy selected filter row background",
    "filterMark": "Alchemy filter row diamond",
    "alchemyMetal": "Alchemy frame, ingredient slots and pot - metal",
    "alchemyStone": "Alchemy frame, ingredient slots and pot - stone",
    "enchantingFrame": "Enchanting right panel frame - metal",
    "spellmakingFrame": "Spellmaking right panel frame - metal",
    "altarPaper": "Enchanting and Spellmaking right panel paper",
    "enchantingCrystal": "Enchanting crystal under the right panel",
    "enchantingOrnament": "Enchanting ornament under the right panel",
    "spellmakingEye": "Spellmaking eye ornament under the right panel",
    "enchantingTabPaper": "Enchanting tab strip background",
    "enchantingTabIcon": "Enchanting tab strip icons and arrows",
    "detailsPage": "Inventory, Magic and shared details page brightness",
}
TAB_HELP = {
    "selected": "Inventory and Magic selected category tab fill",
    "unselected": "Inventory and Magic unselected category tab fill",
    "gap": "Inventory and Magic gaps between category tabs",
    "outline": "Inventory and Magic category tab outlines",
    "glyph": "Inventory and Magic selected category tab icons",
    "glyphDim": "Inventory and Magic unselected category tab icons",
}


# the builder window's subgroups (owner decision 26): every entry -> the menu it changes; baked [Skin] values as "Skin.<name>"
GROUP_ORDER = ("All menus", "Inventory and Magic lists", "Inventory and Magic category tabs", "Inventory details", "Alchemy",
               "Enchanting", "Spellmaking", "Enchanting and Spellmaking", "Inventory and Magic header (run time)",
               "Inventory and Magic details (run time)")
GROUPS = {
    **{k: "All menus" for k in ("Skin.scrollTrack", "Skin.scrollBorder", "Skin.scrollInside", "parchmentDark", "parchmentLight", "nameBox")},
    **{k: "Inventory and Magic lists" for k in ("panel", "frame", "rowText", "rowSeparator", "columnLine", "rowIcon", "headerIcon",
                                                 "pickerIcon", "listHighlight")},
    **{k: "Inventory and Magic category tabs" for k in ("selected", "unselected", "gap", "outline", "glyph", "glyphDim")},
    **{k: "Inventory details" for k in ("subTabDisc", "subTabCircle", "subTabIcon", "pattern", "Skin.pattern", "enchantedDot",
                                         "previewFrameLight", "previewFrameDark", "detailsPage")},
    **{k: "Alchemy" for k in ("effectBox", "filterRow", "filterHover", "filterSelected", "filterMark", "alchemyMetal", "alchemyStone")},
    **{k: "Enchanting" for k in ("enchantingFrame", "enchantingCrystal", "enchantingOrnament", "enchantingTabPaper", "enchantingTabIcon")},
    **{k: "Spellmaking" for k in ("spellmakingFrame", "spellmakingEye")},
    "altarPaper": "Enchanting and Spellmaking",
    **{k: "Inventory and Magic header (run time)" for k in ("rule", "guide")},
    **{k: "Inventory and Magic details (run time)" for k in ("ring", "ornament", "band", "buttonFill", "emphasisInk", "buttonRule")},
}


def from_darkstyle(text):
    """Every Rgb, detailsPage and the tab greys of a DarkStyle.h text (not the run-time skin tints)."""
    values = {n: [float(a), float(b), float(c)] for n, a, b, c in
              re.findall(r"Rgb\s+(\w+)\{\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*\}", text)}
    for name in NUMBERS:
        found = re.search(r"float\s+" + name + r"\s*=\s*([\d.]+)f?", text)
        if found:
            values[name] = float(found.group(1))
    block = re.search(r"struct Tab\s*\{(.*?)\}\s*tab;", text, re.S)
    if block:
        values |= {n: float(v) for n, v in re.findall(r"float\s+(\w+)\s*=\s*([\d.]+)", block.group(1))}
    return values


def from_skin(text):
    """The run-time skin tints of a DarkStyle.h text, in header order."""
    block = re.search(r"struct SkinTints\s*\{(.*?)\}\s*skin;", text, re.S)
    values = {}
    for found in re.finditer(r"array<float,\s*4>\s+(\w+)\{([^}]*)\}|(\w+)\s*=\s*([\d.]+)f", block.group(1) if block else ""):
        if found.group(1):
            values[found.group(1)] = [float(v.strip().rstrip("f")) for v in found.group(2).split(",")]
        else:
            values[found.group(3)] = float(found.group(4))
    return values


def number(v):
    return str(int(v)) if float(v).is_integer() else repr(float(v))


def render(values, skin=None):
    """The text of a CustomColours.ini holding these values (skin: the [Skin] tints, default SKIN)."""
    skin = SKIN if skin is None else skin
    lines = ["; Menus Revamped - Custom colours.",
             "; [Colours]: red, green, blue (0-255) per element; detailsPage is one number. [Tab]: tab strip greys (0-255).",
             "; [Skin]: linear red, green, blue, alpha (0-1); the three scrollbar entries are single numbers.",
             "; Missing entries use Dark defaults. Rebuild for baked colours, including [Skin] pattern and the scrollbar.",
             "; Other active [Skin] edits need a restart. The Effects heading tint is used only when replacement art is unavailable.",
             "; [Colours] rowSeparator and pickerIcon are retained keys with no effect in this build.",
             "", "[Colours]"]
    for name in list(COLOURS) + list(NUMBERS):
        value = values[name]
        text = ", ".join(number(c) for c in value) if isinstance(value, list) else number(value)
        lines.append(f"{name} = {text}".ljust(36) + f"; {HELP[name]}")
    lines += ["", "[Tab]"]
    for name in TAB:
        lines.append(f"{name} = {number(values[name])}".ljust(36) + f"; {TAB_HELP[name]}")
    lines += ["", "[Skin]"]
    for name in SKIN:
        value = skin[name]
        text = ", ".join(number(c) for c in value) if isinstance(value, list) else number(value)
        lines.append(f"{name} = {text}".ljust(36) + f"; {SKIN_HELP[name]}")
    return "\n".join(lines) + "\n"


def parse(name, raw, section="Colours"):
    raw = raw.split(";", 1)[0].strip()
    try:
        parts = [float(p) for p in raw.split(",")]
    except ValueError:
        sys.exit(f"CustomColours.ini: {name} = {raw!r} is not a number list")
    if section == "Skin":
        size = 4 if isinstance(SKIN[name], list) else 1
        if len(parts) != size or not all(0 <= p <= 1 for p in parts):
            sys.exit(f"CustomColours.ini: [Skin] {name} needs {size} number(s) from 0 to 1, found {raw!r}")
        return parts if size == 4 else parts[0]
    if name in COLOURS:
        if len(parts) != 3 or not all(0 <= p <= 255 for p in parts):
            sys.exit(f"CustomColours.ini: {name} needs three numbers from 0 to 255 (red, green, blue), found {raw!r}")
        return parts
    top = 1.0 if name in NUMBERS else 255.0
    if len(parts) != 1 or not 0 <= parts[0] <= top:
        sys.exit(f"CustomColours.ini: {name} needs one number from 0 to {number(top)}, found {raw!r}")
    return parts[0]


SECTIONS = {"Colours": set(COLOURS) | set(NUMBERS), "Tab": set(TAB), "Skin": set(SKIN)}


def read_entries(path):
    """[(section, name, value)] of the known entries of a colour file (unknown ones noted on stderr and skipped)."""
    path = Path(path)
    if not path.is_file():
        return []
    parser = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=None)
    parser.optionxform = str
    parser.read(path, encoding="utf-8-sig")
    found = []
    for section in parser.sections():
        for name, raw in parser.items(section):
            if name not in SECTIONS.get(section, ()):
                print(f"CustomColours.ini: [{section}] {name} is not a known entry, ignored", file=sys.stderr)
                continue
            found.append((section, name, parse(name, raw, section)))
    return found


def load(path=DEFAULT_PATH):
    """The baked colour values: the file's [Colours] and [Tab] entries over DEFAULTS (a missing file = DEFAULTS)."""
    values = {k: (list(v) if isinstance(v, list) else v) for k, v in DEFAULTS.items()}
    values |= {name: value for section, name, value in read_entries(path) if section != "Skin"}
    return values


def load_skin(path=DEFAULT_PATH):
    """The run-time tints: the file's [Skin] entries over SKIN."""
    values = {k: (list(v) if isinstance(v, list) else v) for k, v in SKIN.items()}
    values |= {name: value for section, name, value in read_entries(path) if section == "Skin"}
    return values


def pack_values(path=None):
    """Values the recipes can consume, including the separately named baked Skin tints."""
    colours = load(path) if path else DEFAULTS
    skin = load_skin(path) if path else SKIN
    return colours | {key: skin[name] for name, key in BAKED_SKIN.items()}


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PATH
    values = from_darkstyle((ROOT / "src/core/DarkStyle.h").read_text(encoding="utf-8"))
    missing = sorted(set(DEFAULTS) - set(values))
    if missing:
        sys.exit(f"DarkStyle.h lacks {missing}")
    out.parent.mkdir(parents=True, exist_ok=True)
    skin = from_skin((ROOT / "src/core/DarkStyle.h").read_text(encoding="utf-8"))
    if list(skin) != list(SKIN):
        sys.exit(f"DarkStyle.h skin tints {list(skin)} differ from SKIN {list(SKIN)}")
    out.write_text(render(values, skin), encoding="utf-8", newline="\n")
    print(f"wrote {out} ({len(DEFAULTS)} entries)")


if __name__ == "__main__":
    main()
