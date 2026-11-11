"""The window's colour model: one entry per CustomColours.ini entry, read and written through custom_colours.py (no Tk here).

[Colours]: red, green, blue 0-255 (detailsPage one number 0-1); [Tab]: greys 0-255; [Skin]: linear red, green, blue, alpha
0-1 (the scrollbar entries one number). A swatch is the entry's colour in sRGB; a picked sRGB colour goes back into the entry
([Skin] in linear, its alpha kept).
"""
import configparser
import copy
from dataclasses import dataclass
from pathlib import Path

import custom_colours as cc

BUILD_NOTE = ("[Skin] changes need only a game restart. [Colours] and [Tab] changes are baked into the Custom pack: "
              "they need a Build.")
TAB_HELP = {"selected": "selected tab", "unselected": "other tabs", "gap": "gap between tabs", "outline": "tab outline",
            "glyph": "selected tab icon", "glyphDim": "other tab icons"}


class ModelError(Exception):
    """A colour file that cannot be read: the message names the entry."""


def linear(v):
    c = v / 255
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def srgb(l):
    l = max(0.0, min(1.0, l))
    c = l * 12.92 if l <= 0.0031308 else 1.055 * l ** (1 / 2.4) - 0.055
    return round(c * 255)


def lum(rgb):
    return 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]


@dataclass
class Entry:
    section: str
    name: str
    help: str
    default: object
    value: object

    @property
    def kind(self):
        if self.section == "Skin":
            return "rgba" if isinstance(self.default, list) else "scalar"
        if self.section == "Tab":
            return "grey"
        return "rgb" if isinstance(self.default, list) else "number"


class ColourModel:
    def __init__(self, path):
        self.path = Path(path)
        try:
            found = {(s, n): v for s, n, v in cc.read_entries(self.path)}
        except SystemExit as stop:   # custom_colours stops on a broken value, naming it
            raise ModelError(str(stop.code)) from None
        except (OSError, configparser.Error, UnicodeError) as error:
            raise ModelError(f"{self.path}: {error}") from None
        table = ([("Colours", n, cc.HELP[n], d) for n, d in (cc.COLOURS | cc.NUMBERS).items()]
                 + [("Tab", n, TAB_HELP[n], d) for n, d in cc.TAB.items()]
                 + [("Skin", n, cc.SKIN_HELP[n], d) for n, d in cc.SKIN.items()])
        self.entries = [Entry(s, n, h, copy.deepcopy(d), copy.deepcopy(found.get((s, n), d))) for s, n, h, d in table]
        self._index = {(e.section, e.name): e for e in self.entries}

    def entry(self, section, name):
        return self._index[(section, name)]

    def section(self, section):
        return [e for e in self.entries if e.section == section]

    def value(self, section, name):
        return self.entry(section, name).value

    def text(self, section, name):
        v = self.value(section, name)
        return ", ".join(cc.number(c) for c in v) if isinstance(v, list) else cc.number(v)

    def set_text(self, section, name, text):
        """Takes a typed value with the colour file's ranges; ValueError (the message) and the old value kept otherwise."""
        try:
            value = cc.parse(name, text, section)
        except SystemExit as stop:
            raise ValueError(str(stop.code)) from None
        self.entry(section, name).value = value

    def swatch(self, section, name):
        e = self.entry(section, name)
        v = e.value
        rgb = {"rgb": lambda: [round(c) for c in v], "grey": lambda: [round(v)] * 3,
               "number": lambda: [srgb(v)] * 3, "rgba": lambda: [srgb(c) for c in v[:3]],
               "scalar": lambda: [srgb(v)] * 3}[e.kind]()
        return "#" + "".join(f"{max(0, min(255, c)):02x}" for c in rgb)

    def set_rgb(self, section, name, rgb):
        """A colour from the picker (sRGB 0-255) into the entry."""
        e = self.entry(section, name)
        rgb = [float(round(c)) for c in rgb]
        if e.kind == "rgb":
            e.value = rgb
        elif e.kind == "grey":
            e.value = float(round(lum(rgb)))
        elif e.kind == "rgba":
            e.value = [round(linear(c), 4) for c in rgb] + [e.value[3]]
        else:
            e.value = round(linear(lum(rgb)), 4)

    def reset_to_dark(self):
        for e in self.entries:
            e.value = copy.deepcopy(e.default)

    def needs_build(self, style):
        """Baked entries whose value differs from the pack's manifest style ([Skin] is never baked)."""
        changed = []
        for e in self.entries:
            if e.section == "Skin" or e.name not in style:
                continue
            a = e.value if isinstance(e.value, list) else [e.value]
            b = style[e.name] if isinstance(style[e.name], list) else [style[e.name]]
            if len(a) != len(b) or any(abs(float(x) - float(y)) > 1e-6 for x, y in zip(a, b)):
                changed.append(e.name)
        return changed

    def save(self, path=None):
        """Writes all three sections (a missing file and its folder are created); the file becomes the model's file."""
        path = Path(path) if path else self.path
        values = {e.name: e.value for e in self.entries if e.section != "Skin"}
        skin = {e.name: e.value for e in self.entries if e.section == "Skin"}
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(cc.render(values, skin), encoding="utf-8", newline="\n")
        self.path = path
        return path
