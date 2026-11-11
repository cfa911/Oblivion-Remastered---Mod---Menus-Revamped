"""Custom pack builder Phase 4 (owner 2026-11-11): the window's colour model, testable without Tk.
- one entry per CustomColours.ini entry ([Colours] 25, [Tab] 6, [Skin] 12), each with its help text;
- values typed as text use the colour file's ranges; a wrong value is refused with a message, never written;
- a swatch colour per entry (the preview) and a picker colour back into the entry ([Skin] keeps its alpha);
- Reset to Dark gives today's values; saving writes all three sections and creates a missing file;
- a [Skin] change needs no Build, a [Colours]/[Tab] change does (compared with the pack's manifest);
- a broken colour file gives a message naming the entry (no exit, the window must stay open)."""
import importlib
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
import custom_colours  # noqa: E402

try:
    model = importlib.import_module("pack_builder.model")
except Exception as error:   # the package is new in Phase 4
    model = None
    check(False, f"tools/pack_builder/model.py imports ({type(error).__name__}: {error})")

ColourModel = getattr(model, "ColourModel", None)
ModelError = getattr(model, "ModelError", None)
check(ColourModel is not None, "pack_builder.model has ColourModel")
check(isinstance(ModelError, type) and issubclass(ModelError, Exception) and not issubclass(ModelError, SystemExit),
      "pack_builder.model has ModelError (an exception, not an exit)")


def srgb(l):
    c = l * 12.92 if l <= 0.0031308 else 1.055 * l ** (1 / 2.4) - 0.055
    return round(max(0.0, min(1.0, c)) * 255)


def refused(m, section, name, text):
    before = m.value(section, name)
    try:
        m.set_text(section, name, text)
    except ValueError:
        return m.value(section, name) == before
    except BaseException:
        return False
    return False


with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    if ColourModel:
        m = ColourModel(temp / "missing/CustomColours.ini")
        sizes = {s: len(m.section(s)) for s in ("Colours", "Tab", "Skin")}
        check(sizes == {"Colours": 25, "Tab": 6, "Skin": 12}, f"one entry per colour-file entry, by section ({sizes})")
        check(len(m.entries) == 43, f"43 entries in all ({len(m.entries)})")
        check(all(e.help for e in m.entries), "every entry has its help text (the window's label)")
        check([e.name for e in m.section("Colours")] == list(custom_colours.COLOURS) + list(custom_colours.NUMBERS)
              and [e.name for e in m.section("Skin")] == list(custom_colours.SKIN), "entries keep the colour file's order")
        check(all(m.value("Colours", n) == v for n, v in custom_colours.DEFAULTS.items() if n not in custom_colours.TAB)
              and all(m.value("Skin", n) == v for n, v in custom_colours.SKIN.items()),
              "a missing file gives today's Dark values")

        # text in, text out, the file's ranges
        check(m.text("Colours", "rowText") == "239, 239, 239", f"an entry's text is the file's text ({m.text('Colours', 'rowText')!r})")
        m.set_text("Colours", "rowText", "255, 80, 80")
        check(m.value("Colours", "rowText") == [255.0, 80.0, 80.0], "a typed colour is taken")
        for section, name, text in (("Colours", "rowText", "256, 0, 0"), ("Colours", "rowText", "1, 2"),
                                    ("Colours", "rowText", "red"), ("Colours", "detailsPage", "2"),
                                    ("Tab", "selected", "300"), ("Skin", "band", "1.2, 0, 0, 1"),
                                    ("Skin", "scrollTrack", "0.1, 0.2")):
            check(refused(m, section, name, text), f"[{section}] {name} = {text!r} is refused with a ValueError, value kept")
        m.set_text("Skin", "scrollTrack", "0.5")
        check(m.value("Skin", "scrollTrack") == 0.5, "a one-number [Skin] entry takes one number")

        # swatches and picker colours
        check(m.swatch("Colours", "rowText") == "#ff5050", f"an RGB swatch ({m.swatch('Colours', 'rowText')})")
        check(m.swatch("Tab", "selected") == "#292929", f"a [Tab] grey swatch ({m.swatch('Tab', 'selected')})")
        fill = srgb(0.12)
        check(m.swatch("Skin", "buttonFill") == f"#{fill:02x}{fill:02x}{fill:02x}",
              f"a [Skin] swatch shows the linear tint in sRGB ({m.swatch('Skin', 'buttonFill')})")
        m.set_rgb("Skin", "rule", (255, 0, 0))
        check(m.value("Skin", "rule") == [1.0, 0.0, 0.0, 0.55], f"a picked [Skin] colour is linear and keeps its alpha ({m.value('Skin', 'rule')})")
        m.set_rgb("Tab", "selected", (60, 60, 60))
        check(m.value("Tab", "selected") == 60.0, "a picked grey sets a [Tab] entry")
        m.set_rgb("Colours", "panel", (10, 20, 30))
        check(m.value("Colours", "panel") == [10.0, 20.0, 30.0], "a picked colour sets a [Colours] entry")

        # a Build is needed for baked values only
        manifest = json.loads((root / "assets/CustomPack/MRCustom_P.json").read_text(encoding="utf-8"))
        fresh = ColourModel(temp / "none.ini")
        fresh.set_text("Skin", "band", "1, 0, 0, 1")
        check(fresh.needs_build(manifest["style"]) == [], "a [Skin] change needs no Build (run-time tint)")
        fresh.set_text("Colours", "rowText", "255, 80, 80")
        fresh.set_text("Tab", "glyph", "200")
        check(sorted(fresh.needs_build(manifest["style"])) == ["glyph", "rowText"],
              f"[Colours] and [Tab] changes need a Build ({fresh.needs_build(manifest['style'])})")
        note = getattr(model, "BUILD_NOTE", "")
        check("[Skin]" in note and "restart" in note and "Build" in note,
              "the window says [Skin] changes need a game restart and [Colours]/[Tab] changes a Build")

        # save: all three sections, a missing file and folder are created
        target = temp / "Plugins/MenusRevamped/CustomColours.ini"
        m.save(target)
        check(target.is_file(), "saving creates a missing colour file (and its folder)")
        text = target.read_text(encoding="utf-8") if target.is_file() else ""
        check(all(s in text for s in ("[Colours]", "[Tab]", "[Skin]")), "the saved file has all three sections")
        check(custom_colours.load(target)["rowText"] == [255.0, 80.0, 80.0] and custom_colours.load(target)["selected"] == 60.0,
              "the saved values read back through custom_colours.load")
        check(custom_colours.load_skin(target)["rule"] == [1.0, 0.0, 0.0, 0.55], "the saved [Skin] values read back")
        again = ColourModel(target)
        check([again.value(e.section, e.name) for e in again.entries] == [m.value(e.section, e.name) for e in m.entries],
              "a saved file loads back with the same values")

        # Reset to Dark
        m.reset_to_dark()
        check(all(m.value(e.section, e.name) == e.default for e in m.entries), "Reset to Dark gives every default")
        check(m.value("Colours", "rowText") == [239.0, 239.0, 239.0] and m.value("Skin", "rule") == [0.65, 0.65, 0.65, 0.55],
              "Reset to Dark restores today's values")
        m.save(target)
        check(target.read_text(encoding="utf-8") == custom_colours.render(custom_colours.DEFAULTS),
              "a reset and saved file is the default file")

        # a broken file
        broken = temp / "broken.ini"
        broken.write_text("[Colours]\nrowText = red\n", encoding="utf-8")
        try:
            ColourModel(broken)
            check(False, "a broken colour file is refused")
        except BaseException as error:
            ok = ModelError is not None and isinstance(error, ModelError)
            check(ok and "rowText" in str(error), f"a broken file gives a ModelError naming the entry ({type(error).__name__}: {error})")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
