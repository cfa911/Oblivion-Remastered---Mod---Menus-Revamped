"""The Custom pack's colour file, CustomColours.ini: one value per element (today's DarkStyle.h names), read by the pack recipes.

Usage:
  python tools/custom_colours.py [<output .ini>]   write the default file from src/core/DarkStyle.h (default config/CustomColours.ini)

load(path) returns {element: value}: [r, g, b] (sRGB 0-255) for the [Colours] entries, a number for detailsPage and the [Tab]
greys. A missing file or key gives the built-in default (DEFAULTS, today's Dark values); an unknown key is ignored with a note;
a broken value stops with a message that names the key. Colours are per element for now; merging them into roles is a later,
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
}
NUMBERS = {"detailsPage": 0.010}   # [Colours] entries that are one number (detailsPage: a linear multiply, 0..1)
TAB = {"selected": 41.0, "unselected": 33.0, "gap": 24.0, "outline": 134.0, "glyph": 242.0, "glyphDim": 162.0}
DEFAULTS = COLOURS | NUMBERS | TAB

HELP = {
    "panel": "list page", "frame": "list page edge and scrollbar", "rowText": "row and details text",
    "rowSeparator": "row separator line", "columnLine": "row column lines", "rowIcon": "row icons",
    "headerIcon": "header sort icons", "pickerIcon": "sort picker icons", "listHighlight": "hovered row band",
    "subTabDisc": "selected details sub-tab disc", "subTabCircle": "details sub-tab circle", "subTabIcon": "details sub-tab icons",
    "pattern": "laurels beside the action button", "enchantedDot": "enchanted item dot",
    "previewFrameLight": "item preview frame, light end", "previewFrameDark": "item preview frame, dark end",
    "parchmentDark": "parchment pages, dark end", "parchmentLight": "parchment pages, light end",
    "effectBox": "Alchemy effect boxes", "nameBox": "editable name fields", "filterRow": "Alchemy filter rows",
    "filterHover": "Alchemy filter row under the cursor", "filterSelected": "Alchemy selected filter row",
    "filterMark": "Alchemy filter diamond", "detailsPage": "details page darkness (0 black .. 1 stock)",
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


def number(v):
    return str(int(v)) if float(v).is_integer() else repr(float(v))


def render(values):
    """The text of a CustomColours.ini holding these values."""
    lines = ["; Menus Revamped - Custom colours.",
             "; [Colours]: red, green, blue (0-255) per element; detailsPage is one number. [Tab]: tab strip greys (0-255).",
             "; A missing entry uses the default Dark colour. Rebuild the Custom pack after a change.",
             "", "[Colours]"]
    for name in list(COLOURS) + list(NUMBERS):
        value = values[name]
        text = ", ".join(number(c) for c in value) if isinstance(value, list) else number(value)
        lines.append(f"{name} = {text}".ljust(36) + f"; {HELP[name]}")
    lines += ["", "[Tab]"]
    lines += [f"{name} = {number(values[name])}" for name in TAB]
    return "\n".join(lines) + "\n"


def parse(name, raw):
    raw = raw.split(";", 1)[0].strip()
    try:
        parts = [float(p) for p in raw.split(",")]
    except ValueError:
        sys.exit(f"CustomColours.ini: {name} = {raw!r} is not a number list")
    if name in COLOURS:
        if len(parts) != 3 or not all(0 <= p <= 255 for p in parts):
            sys.exit(f"CustomColours.ini: {name} needs three numbers from 0 to 255 (red, green, blue), found {raw!r}")
        return parts
    top = 1.0 if name in NUMBERS else 255.0
    if len(parts) != 1 or not 0 <= parts[0] <= top:
        sys.exit(f"CustomColours.ini: {name} needs one number from 0 to {number(top)}, found {raw!r}")
    return parts[0]


def load(path=DEFAULT_PATH):
    """The colour values: the file's entries over DEFAULTS (a missing file = DEFAULTS)."""
    values = {k: (list(v) if isinstance(v, list) else v) for k, v in DEFAULTS.items()}
    path = Path(path)
    if not path.is_file():
        return values
    parser = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=None)
    parser.optionxform = str
    parser.read(path, encoding="utf-8-sig")
    sections = {"Colours": set(COLOURS) | set(NUMBERS), "Tab": set(TAB)}
    for section in parser.sections():
        for name, raw in parser.items(section):
            if name not in sections.get(section, ()):
                print(f"CustomColours.ini: [{section}] {name} is not a known entry, ignored", file=sys.stderr)
                continue
            values[name] = parse(name, raw)
    return values


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PATH
    values = from_darkstyle((ROOT / "src/core/DarkStyle.h").read_text(encoding="utf-8"))
    missing = sorted(set(DEFAULTS) - set(values))
    if missing:
        sys.exit(f"DarkStyle.h lacks {missing}")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(render(values), encoding="utf-8", newline="\n")
    print(f"wrote {out} ({len(DEFAULTS)} entries)")


if __name__ == "__main__":
    main()
