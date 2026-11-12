"""The window's colour and size model: one entry per CustomColours.ini entry, read and written through custom_colours.py (no Tk).

[Colours]: red, green, blue 0-255 (detailsPage one number 0-1); [Tab]: greys 0-255; [Skin]: linear red, green, blue, alpha
0-1 (the scrollbar entries one number). A swatch is the entry's colour in sRGB; a picked sRGB colour goes back into the entry
([Skin] in linear, its alpha kept).

Step B (owner 2026-11-12, decisions 38-45): roles feed their elements (custom_colours.ROLES); a role change shifts every element
of the role by the same per-channel amount in sRGB (each element's tint kept, decision 43); an element set by hand is an "own
value" until reset. A file's element value that differs from what its role gives loads as an own value, so files written
before roles keep every value. [Sizes]: text role scales (percent, decision 42) feed the font sizes; row sizes per list.
Tabs (decision 38): Colours (roles), Text, Rows, Runtime (our own widgets only).
"""
import configparser
import copy
from dataclasses import dataclass
from pathlib import Path

import custom_colours as cc

BUILD_NOTE = ("Build packs the colours and sizes. Save runtime colours and restart the game to apply them.")
TOLERANCE = 1e-4


class ModelError(Exception):
    """A colour file that cannot be read: the message names the entry."""


def linear(v):
    c = v / 255
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def srgb(l):
    l = max(0.0, min(1.0, l))
    c = l * 12.92 if l <= 0.0031308 else 1.055 * l ** (1 / 2.4) - 0.055
    return round(c * 255)


def srgb_exact(l):
    l = max(0.0, min(1.0, l))
    c = l * 12.92 if l <= 0.0031308 else 1.055 * l ** (1 / 2.4) - 0.055
    return c * 255


def lum(rgb):
    return 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]


def same(a, b):
    a = a if isinstance(a, list) else [a]
    b = b if isinstance(b, list) else [b]
    return len(a) == len(b) and all(abs(float(x) - float(y)) <= TOLERANCE for x, y in zip(a, b))


@dataclass
class Entry:
    section: str
    name: str
    help: str
    default: object
    value: object

    @property
    def key(self):
        """The role table's name: [Colours] names as they are, "Skin.<name>", "Tab.<name>"."""
        return self.name if self.section == "Colours" else f"{self.section}.{self.name}"

    @property
    def applies(self):
        if self.section == "Skin":
            if self.name in cc.RETIRED_SKIN:
                return "Inactive"   # retired by phase 28b: the band art and the button prefab are baked
            return "Build pack" if self.name in cc.BAKED_SKIN else "Restart game"
        if self.section == "Tab":
            return "Inactive"   # retired by decision 34: the tabs bake from tabPaperDark/tabPaperLight/tabGlyph
        return "Inactive" if self.name in ("rowSeparator", "pickerIcon") else "Build pack"

    @property
    def kind(self):
        if self.section == "Skin":
            return "rgba" if isinstance(self.default, list) else "scalar"
        if self.section == "Tab":
            return "grey"
        return "rgb" if isinstance(self.default, list) else "number"


@dataclass
class SizeEntry:
    key: str        # the [Sizes] name
    help: str
    default: float
    kind: str       # "scale" (a text role, percent), "font" (an element font size), "row" (a Rows tab size)

    section = "Sizes"


def to_srgb(kind, v):
    """An element value as sRGB floats (0-255, unrounded) for the role arithmetic."""
    return {"rgb": lambda: [float(c) for c in v], "grey": lambda: [float(v)] * 3,
            "number": lambda: [srgb_exact(v)] * 3, "rgba": lambda: [srgb_exact(c) for c in v[:3]],
            "scalar": lambda: [srgb_exact(v)] * 3}[kind]()


def from_srgb(kind, rgb, like):
    """sRGB floats back into an element value of this kind (like: the element's value, for its alpha)."""
    rgb = [max(0.0, min(255.0, c)) for c in rgb]
    if kind == "rgb":
        return [float(round(c)) for c in rgb]
    if kind == "grey":
        return float(round(lum(rgb)))
    if kind == "rgba":
        return [round(linear(c), 4) for c in rgb] + [like[3]]
    return round(linear(lum(rgb)), 4)


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
                 + [("Tab", n, cc.TAB_HELP[n], d) for n, d in cc.TAB.items()]
                 + [("Skin", n, cc.SKIN_HELP[n], d) for n, d in cc.SKIN.items()])
        self.entries = [Entry(s, n, h, copy.deepcopy(d), copy.deepcopy(d)) for s, n, h, d in table]
        self._index = {(e.section, e.name): e for e in self.entries}
        self._by_key = {e.key: e for e in self.entries}
        self._role_of = {m: r for r, role in cc.ROLES.items() for m in role["members"]}
        self.role_defaults = {r: to_srgb(self._by_key[role["members"][0]].kind, self._by_key[role["members"][0]].default)
                              for r, role in cc.ROLES.items()}
        self.roles = {r: [float(c) for c in found.get(("Roles", r), [round(c) for c in d])] for r, d in self.role_defaults.items()}
        self.own = set()
        for e in self.entries:
            derived = self.derived(e.key)
            e.value = copy.deepcopy(found.get((e.section, e.name), derived if derived is not None else e.default))
            if derived is not None and not same(e.value, derived):
                self.own.add(e.key)
        # sizes
        self.size_entries = ([SizeEntry(k, r["label"], 100, "scale") for k, r in cc.TEXT_ROLES.items()]
                             + [SizeEntry(f"font.{m}", m.replace(".", ": "), cc.FONT_SIZES[m], "font")
                                for r in cc.TEXT_ROLES.values() for m in r["members"]]
                             + [SizeEntry(k, f"{label} - {what}", v, "row") for label, rows in cc.ROW_LISTS for k, what, v in rows])
        self._size_index = {s.key: s for s in self.size_entries}
        self._scale_of = {f"font.{m}": k for k, r in cc.TEXT_ROLES.items() for m in r["members"]}
        self.sizes = {k: float(v) for k, v in cc.SIZES.items()}
        for (section, name), v in found.items():
            if section == "Sizes":
                self.sizes[name] = float(v)
        self.own_sizes = {k for k in self._scale_of if abs(self.sizes[k] - self.derived_font(k)) > TOLERANCE}

    # ---- entries ----
    def entry(self, section, name):
        return self._index[(section, name)]

    def by_key(self, key):
        return self._by_key[key]

    def section(self, section):
        return [e for e in self.entries if e.section == section]

    def value(self, section, name):
        return self.entry(section, name).value

    def text(self, section, name):
        v = self.value(section, name)
        return ", ".join(cc.number(c) for c in v) if isinstance(v, list) else cc.number(v)

    def set_text(self, section, name, text):
        """Takes a typed value with the colour file's ranges; ValueError (the message) and the old value kept otherwise. An element
        of a role becomes an own value."""
        try:
            value = cc.parse(name, text, section)
        except SystemExit as stop:
            raise ValueError(str(stop.code)) from None
        e = self.entry(section, name)
        e.value = value
        self._mark(e)

    def swatch(self, section, name):
        e = self.entry(section, name)
        return self.hex(to_srgb(e.kind, e.value))

    @staticmethod
    def hex(rgb):
        return "#" + "".join(f"{max(0, min(255, round(c))):02x}" for c in rgb)

    def set_rgb(self, section, name, rgb):
        """A colour from the picker (sRGB 0-255) into the entry (an own value when the element belongs to a role)."""
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
        self._mark(e)

    def _mark(self, e):
        derived = self.derived(e.key)
        if derived is not None:
            (self.own.discard if same(e.value, derived) else self.own.add)(e.key)

    # ---- roles (decisions 39, 43) ----
    def role_of(self, key):
        return self._role_of.get(key)

    def role_value(self, role):
        return list(self.roles[role])

    def role_swatch(self, role):
        return self.hex(self.roles[role])

    def derived(self, key):
        """What the element's role gives it (None for an element without a role): its Dark default shifted by the role change."""
        role = self._role_of.get(key)
        if role is None:
            return None
        e = self._by_key[key]
        delta = [a - b for a, b in zip(self.roles[role], [round(c) for c in self.role_defaults[role]])]
        if not any(delta):
            return copy.deepcopy(e.default)
        base = to_srgb(e.kind, e.default)
        if e.kind in ("number", "scalar", "grey"):
            shift = sum(delta) / 3
            return from_srgb(e.kind, [c + shift for c in base], e.default)
        return from_srgb(e.kind, [c + d for c, d in zip(base, delta)], e.default)

    def set_role(self, role, rgb):
        """A role colour (sRGB 0-255): every element of the role that is not an own value follows."""
        rgb = [float(round(c)) for c in rgb]
        if len(rgb) != 3 or not all(0 <= c <= 255 for c in rgb):
            raise ValueError(f"{cc.ROLES[role]['label']} needs three numbers from 0 to 255")
        self.roles[role] = rgb
        for key in cc.ROLES[role]["members"]:
            if key not in self.own:
                self._by_key[key].value = self.derived(key)

    def set_role_text(self, role, text):
        try:
            rgb = cc.parse(role, text, "Roles")
        except SystemExit as stop:
            raise ValueError(str(stop.code)) from None
        self.set_role(role, rgb)

    def role_text(self, role):
        return ", ".join(cc.number(c) for c in self.roles[role])

    def is_own(self, key):
        return key in self.own

    def reset_element(self, key):
        """Puts an element back on its role (Advanced: "Reset")."""
        derived = self.derived(key)
        if derived is not None:
            self._by_key[key].value = derived
            self.own.discard(key)

    # ---- sizes (decisions 29, 33, 42, 45) ----
    def size_entry(self, key):
        return self._size_index[key]

    def size(self, key):
        return self.sizes[key]

    def derived_font(self, key):
        scale = self.sizes[self._scale_of[key]]
        return round(cc.FONT_SIZES[key[len("font."):]] * scale / 100 * 2) / 2

    def font_size(self, member):
        return self.sizes[member if member.startswith("font.") else f"font.{member}"]

    def size_text(self, key):
        return cc.number(self.sizes[key])

    def set_size(self, key, value):
        """A [Sizes] value with its range (ValueError, old value kept); a text role scale moves its fonts that are not own."""
        try:
            value = cc.parse(key, str(value), "Sizes")
        except SystemExit as stop:
            raise ValueError(str(stop.code)) from None
        self.sizes[key] = value
        if key in self._scale_of:
            (self.own_sizes.discard if abs(value - self.derived_font(key)) <= TOLERANCE else self.own_sizes.add)(key)
        if key in cc.TEXT_ROLES:
            for member in cc.TEXT_ROLES[key]["members"]:
                font = f"font.{member}"
                if font not in self.own_sizes:
                    self.sizes[font] = self.derived_font(font)

    def set_font_size(self, member, value):
        self.set_size(member if member.startswith("font.") else f"font.{member}", value)

    def is_own_size(self, key):
        return key in self.own_sizes

    def reset_size(self, key):
        if key in self._scale_of:
            self.sizes[key] = self.derived_font(key)
            self.own_sizes.discard(key)

    # ---- tabs (decision 38) ----
    def pages(self):
        """[(tab, [(heading, [entries])])]: Colours = one group per role (its elements under it, shown with Advanced), Text = one
        group per text role (its scale first, then its font sizes), Rows = one group per list, Runtime = our own widgets."""
        colours = [(cc.ROLES[r]["label"], [self._by_key[m] for m in cc.ROLES[r]["members"]]) for r in cc.ROLES]
        text = [(r["label"], [self._size_index[k]] + [self._size_index[f"font.{m}"] for m in r["members"]])
                for k, r in cc.TEXT_ROLES.items()]
        rows = [(label, [self._size_index[k] for k, _, _ in entries]) for label, entries in cc.ROW_LISTS]
        runtime = [("Inventory and Magic header (our own lines, applied at the next game start)",
                    [self.entry("Skin", n) for n in cc.RUNTIME_SKIN])]
        return [("Colours", colours), ("Text", text), ("Rows", rows), ("Runtime", runtime)]

    # ---- reset, build state, save ----
    def reset_to_dark(self):
        for e in self.entries:
            e.value = copy.deepcopy(e.default)
        self.roles = {r: [float(round(c)) for c in d] for r, d in self.role_defaults.items()}
        self.own.clear()
        self.sizes = {k: float(v) for k, v in cc.SIZES.items()}
        self.own_sizes.clear()

    def needs_build(self, style):
        """Baked entries whose value differs from the pack's manifest style."""
        changed = []
        for e in self.entries:
            key = cc.BAKED_SKIN.get(e.name) if e.section == "Skin" else e.name
            if key not in style:
                if key in cc.BAKED_SKIN.values():
                    changed.append(key)
                continue
            if not same(e.value, style[key]):
                changed.append(key)
        return changed

    def sizes_changed(self, baked):
        """[Sizes] values that differ from the layout pack's manifest sizes (baked: {} when there is no layout pack)."""
        return [k for k in cc.SIZES if k not in cc.TEXT_ROLES and (k not in baked or abs(float(baked[k]) - self.sizes[k]) > TOLERANCE)]

    def save(self, path=None):
        """Writes every section (a missing file and its folder are created); the file becomes the model's file."""
        path = Path(path) if path else self.path
        values = {e.name: e.value for e in self.entries if e.section != "Skin"}
        skin = {e.name: e.value for e in self.entries if e.section == "Skin"}
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(cc.render(values, skin, self.roles, self.sizes), encoding="utf-8", newline="\n")
        self.path = path
        return path
