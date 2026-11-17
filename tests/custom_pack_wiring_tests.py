"""Custom pack builder Phase 3 (owner 2026-11-11): the plugin side is wired. The shipped pack is MRCustom_P (assets/CustomPack,
Plugins/MenusRevamped/CustomPack); the plugin reads Plugins/MenusRevamped/CustomColours.ini at load (and writes the default
file when it is missing: the zip ships none, owner decision 8), checks MRCustom_P.json against the running game build, and the
colour file has a [Skin] section for the run-time tints (owner decision 5)."""
import importlib.util
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


def text(rel):
    path = root / rel
    return path.read_text(encoding="utf-8-sig") if path.is_file() else ""


def load_module(name):
    spec = importlib.util.spec_from_file_location(name, root / f"tools/{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


root = source_paths.ROOT

# --- the pack files ---
# preset rework (owner 2026-11-17): the zip ships the Dark pack MRDark_P (assets/DarkPack); MRCustom_P is the player's own
# Pack Builder output (Plugins/MenusRevamped/CustomPack) and is never shipped
dark = root / "assets/DarkPack"
check(all((dark / f"MRDark_P.{e}").is_file() for e in ("pak", "utoc", "ucas", "json")),
      "the shipped pack MRDark_P (.pak/.utoc/.ucas/.json) is in assets/DarkPack")
check(not (root / "assets/CustomPack").exists(), "assets/CustomPack is gone: the Custom pack is the player's build")

sys.path.insert(0, str(root / "tools"))
files = load_module("plugin_files").entries(root)
prefix = "OblivionRemastered/Binaries/Win64/OBSE/Plugins/MenusRevamped/"
for ext in ("pak", "utoc", "ucas", "json"):
    check(files.get(prefix + f"DarkPack/MRDark_P.{ext}") == dark / f"MRDark_P.{ext}",
          f"the zip ships MRDark_P.{ext} in Plugins/MenusRevamped/DarkPack")
check(not any("MRCustom_P" in k or "/CustomPack/" in k for k in files), "the zip ships no MRCustom_P (the player builds it)")
check(not any(k.endswith("CustomColours.ini") for k in files),
      "the zip ships no CustomColours.ini (a Vortex update would replace the user's colours; the plugin writes it)")

mdp = load_module("make_dark_pack")
check(getattr(mdp, "DEFAULT_PACK", None) == "MRCustom_P", "the Pack Builder builds MRCustom_P by default")
check('ROOT / "assets/DarkPack"' in text("tools/make_dark_pack.py") and '"MRDark_P"' in text("tools/make_dark_pack.py"),
      "make_dark_pack.py from source builds the shipped MRDark_P into assets/DarkPack by default")

# --- the colour file: [Skin] section ---
style = text("src/core/DarkStyle.h")
block = re.search(r"struct SkinTints\s*\{(.*?)\}\s*skin;", style, re.S)
skin_names = [m.group(1) or m.group(2) for m in re.finditer(r"array<float,\s*4>\s+(\w+)\{|(\w+)\s*=\s*[\d.]+f", block.group(1))] if block else []
check(len(skin_names) == 16, f"DarkStyle.h has 16 run-time skin tints (C.3 round 2: + 4 Alchemy; {skin_names})")
cc = load_module("custom_colours")
check(list(getattr(cc, "SKIN", {})) == skin_names, "custom_colours.SKIN lists the 12 tints in DarkStyle.h order")
if block and hasattr(cc, "from_skin"):
    check(cc.from_skin(style) == cc.SKIN, "custom_colours.SKIN holds today's DarkStyle.h tint values")
else:
    check(False, "custom_colours.from_skin() reads the tints from DarkStyle.h")
ini = text("config/CustomColours.ini")
check("[Skin]" in ini and "rule = 0.1248, 0.1248, 0.1248, 1" in ini and "scrollTrack = 0.6445" in ini,
      "config/CustomColours.ini has a [Skin] section (four linear values per tint, single numbers for the scrollbar)")
with tempfile.TemporaryDirectory() as temp:
    path = Path(temp) / "skin.ini"
    path.write_text("[Skin]\nrule = 0.1, 0.2, 0.3, 0.4\n", encoding="utf-8")
    import io
    import contextlib
    err = io.StringIO()
    with contextlib.redirect_stderr(err):
        values = cc.load(path)
    check("not a known entry" not in err.getvalue(), "the pack builder accepts [Skin] entries (known keys, no 'ignored' note)")
    check(values == cc.DEFAULTS, "the [Skin] tints never change the baked colours (the pack does not use them)")
    load_skin = getattr(cc, "load_skin", None)
    check(load_skin is not None and load_skin(path).get("rule") == [0.1, 0.2, 0.3, 0.4], "custom_colours.load_skin() reads [Skin]")

# --- the plugin wiring ---
plugin = text("src/core/Plugin.cpp")
check("darkpack::Apply(obse->RuntimeVersion()" in plugin, "Plugin.cpp passes the running game build to the pack switch")
pack = text("src/core/DarkPack.cpp")
check('L"CustomColours.ini"' in pack and 'L"MenusRevamped"' in pack, "DarkPack.cpp reads Plugins/MenusRevamped/CustomColours.ini")
check("customcolours::Parse(" in pack and "darkstyle::Set(" in pack, "DarkPack.cpp parses the colour file and makes it the values in use")
check("customcolours::Render(" in pack, "DarkPack.cpp writes the default colour file when it is missing")
check("custompack::ParseManifest(" in pack and "custompack::Decide(" in pack and 'L"MRCustom_P.json"' in pack,
      "DarkPack.cpp checks MRCustom_P.json before the switch")
check("presetpacks::Switch(" in pack and 'L"CustomPack"' in pack and 'L"DarkPack"' in pack,
      "DarkPack.cpp switches MRCustom_P from CustomPack and MRDark_P from DarkPack")
check("TEMP-DIAG" not in pack, "no temporary diagnostics in DarkPack.cpp")
theme = text("src/core/UiTheme.h")
check("inline constexpr Skin kDarkSkin" not in theme and "darkstyle::Get()" in theme,
      "the Dark skin is built from darkstyle::Get() at run time (not a constexpr copy of the defaults)")
check("inline const Style& Get() { return kDefaults; }" not in style, "darkstyle::Get() no longer returns the compiled-in defaults only")
readme, defaults_ini = text("README.md"), text("config/MenusRevamped.defaults.ini")
check("MRCustom_P" in readme and "CustomColours.ini" in readme and "MRDark_P" in readme,
      "README names MRCustom_P, CustomColours.ini and the shipped MRDark_P")
check("MRCustom_P" in defaults_ini.split("uPreset", 1)[0][-700:], "the INI preset comment names the MRCustom_P pack")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
