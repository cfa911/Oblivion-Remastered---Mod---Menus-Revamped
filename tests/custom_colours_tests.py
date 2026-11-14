"""Custom pack builder Phase 1 (owner 2026-11-11): the pack recipes read their colours from CustomColours.ini (one value per
element, today's names; no role merge yet), generated from today's DarkStyle.h. A missing file or key falls back to today's
Dark value. The pack name is a parameter (default MRCustom_P since Phase 3)."""
import importlib.util
import inspect
import re
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


def load_module(name):
    path = root / f"tools/{name}.py"
    if not path.is_file():
        check(False, f"tools/{name}.py exists")
        return None
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
    except Exception as error:   # noqa: BLE001
        check(False, f"tools/{name}.py imports ({error})")
        return None
    return module


root = source_paths.ROOT
style_text = (root / "src/core/DarkStyle.h").read_text(encoding="utf-8")
# today's values straight from DarkStyle.h: every Rgb, detailsPage, the tab greys (not the run-time skin tints)
RGB = {n: [float(a), float(b), float(c)] for n, a, b, c in
       re.findall(r"Rgb\s+(\w+)\{\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*\}", style_text)}
TAB = {n: float(v) for n, v in re.findall(r"float\s+(\w+)\s*=\s*([\d.]+)",
                                         re.search(r"struct Tab\s*\{(.*?)\}\s*tab;", style_text, re.S).group(1))}
TODAY = RGB | {"detailsPage": 0.010} | TAB
check(len(RGB) == 45 and len(TAB) == 6, f"DarkStyle.h has 45 Rgb values (C.3 2026-11-14: + filterOutline; 2026-11-14: + 5 accent colours; Phase 7 adds 8 Enchanting/Spellmaking keys, 28a rowEquipped, decision 34 three tab keys, 2026-11-13 stolenMark) and 6 retained tab greys ({len(RGB)}, {len(TAB)})")

cc = load_module("custom_colours")
if cc is not None:
    defaults = getattr(cc, "DEFAULTS", {})
    check(defaults == TODAY, "custom_colours.DEFAULTS are today's DarkStyle.h values, one per element (31 keys)")
    from_header = getattr(cc, "from_darkstyle", None)
    check(from_header is not None and from_header(style_text) == TODAY, "custom_colours.from_darkstyle() reads DarkStyle.h")
    load = getattr(cc, "load", None)
    render = getattr(cc, "render", None)
    check(load is not None and render is not None, "custom_colours has load(path) and render(values)")
    ini = root / "config/CustomColours.ini"
    check(ini.is_file(), "config/CustomColours.ini exists (the default colour file)")
    if load and render and ini.is_file():
        check(load(ini) == TODAY, "the committed CustomColours.ini holds today's values")
        check(ini.read_text(encoding="utf-8") == render(TODAY), "CustomColours.ini is the generated file (python tools/custom_colours.py)")
        text = ini.read_text(encoding="utf-8")
        check("[Colours]" in text and "[Tab]" in text and "panel = 41, 41, 41" in text and "detailsPage = 0.01" in text
              and "glyphDim = 162" in text, "CustomColours.ini: [Colours] r, g, b per element, detailsPage, a [Tab] section of greys")
        check("DarkStyle" not in text, "CustomColours.ini speaks to users (no source identifiers)")
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            check(load(temp / "missing.ini") == TODAY, "a missing CustomColours.ini falls back to today's Dark values")
            part = temp / "part.ini"
            part.write_text("[Colours]\nframe =  1 ,2,  3\n; comment\n[Tab]\nglyph = 200\n", encoding="utf-8")
            got = load(part)
            check(got.get("frame") == [1.0, 2.0, 3.0] and got.get("glyph") == 200.0, "set keys are read (spaces tolerated)")
            check({k: v for k, v in got.items() if k not in ("frame", "glyph")} == {k: v for k, v in TODAY.items() if k not in ("frame", "glyph")},
                  "every missing key falls back to today's Dark value")
            extra = temp / "extra.ini"
            extra.write_text("[Colours]\nnotAColour = 1, 2, 3\n", encoding="utf-8")
            check(load(extra) == TODAY, "an unknown key is ignored")
            for bad_text, why in (("[Colours]\npanel = 1, 2\n", "two numbers for a colour"),
                                  ("[Colours]\npanel = 300, 0, 0\n", "a channel above 255"),
                                  ("[Colours]\npanel = red\n", "a word"),
                                  ("[Tab]\nglyph = -4\n", "a negative grey")):
                bad = temp / "bad.ini"
                bad.write_text(bad_text, encoding="utf-8")
                try:
                    load(bad)
                    check(False, f"a broken value is refused with a message ({why})")
                except SystemExit as error:
                    check("panel" in str(error) or "glyph" in str(error), f"the refusal names the key ({why})")

mdp = load_module("make_dark_pack")
tool_text = (root / "tools/make_dark_pack.py").read_text(encoding="utf-8")
check("def style_rgb" not in tool_text and "def style_float" not in tool_text and "re.search" not in tool_text,
      "make_dark_pack.py no longer regex-reads DarkStyle.h (colours come from CustomColours.ini)")
if mdp is not None:
    values = getattr(mdp, "recipe_values", None)
    check(values is not None, "make_dark_pack has recipe_values(recipe, colours)")
    if values and cc is not None:
        colours = dict(getattr(cc, "DEFAULTS", {}))
        colours["frame"] = [1.0, 2.0, 3.0]
        colours["selected"] = 50.0
        check(values("flat:frame", colours) == {"frame": [1.0, 2.0, 3.0]}, "a flat recipe takes its colour from the colour file")
        check(values("tab:0", colours).get("selected") == 50.0, "the tab recipes take the tab greys from the colour file")
        check(values("details_page", colours) == {"detailsPage": 0.010}, "details_page takes detailsPage from the colour file")
        check(values("bp_colour:WBP_ModernMenu_FilterEffectEntry", colours) == {"filterRow": colours["filterRow"], "filterSelected": colours["filterSelected"], "rowText": colours["rowText"]},
              "a blueprint recipe takes its elements from the colour file")
    check(getattr(mdp, "panel_gray", lambda v: None)(0) == 16, "panel_gray's default frame is today's value without DarkStyle.h")
    # the pack name is a parameter
    check(getattr(mdp, "DEFAULT_PACK", None) == "MRCustom_P", "the default pack name is MRCustom_P (Phase 3)")
    check(not hasattr(mdp, "PACK"), "no fixed PACK constant: the name is passed in")
    check("pack" in inspect.signature(getattr(mdp, "build", lambda: None)).parameters, "build() takes the pack name")
    check("colours" in inspect.signature(getattr(mdp, "build", lambda: None)).parameters, "build() takes the colour values")
    name_check = getattr(mdp, "pack_name", None)
    check(name_check is not None, "make_dark_pack has pack_name() (validates --pack)")
    if name_check:
        check(name_check("MRCustom_P") == "MRCustom_P" and name_check("MRDark_P") == "MRDark_P", "valid pack names pass")
        for bad in ("MRDark", "MR Dark_P", "../x_P", "", "MRDark_P.pak"):
            try:
                name_check(bad)
                check(False, f"pack name {bad!r} is refused (letters/digits and a _P suffix only)")
            except SystemExit:
                check(True, "")
    check("--pack" in tool_text and "--colours" in tool_text, "make_dark_pack.py has --pack and --colours options")
    check("config/CustomColours.ini" in tool_text, "--colours defaults to config/CustomColours.ini")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
