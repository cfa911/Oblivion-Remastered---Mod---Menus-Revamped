"""Preset rework (owner 2026-11-17, decision 48 extension): uPreset 0 Classic, 1 Dark, 2 Custom; Light is removed everywhere.

- Classic takes over Light's search bar (approved NOW/MINE sheet): the built-in box in Light's colours; the four
  Search/SearchBar*.png files and their loading code are gone (repo and zip).
- Dark (1) is static: the zip ships Plugins/MenusRevamped/DarkPack/MRDark_P.* (default colours, manifest pack MRDark_P);
  Custom (2) uses the player's Pack Builder output in Plugins/MenusRevamped/CustomPack (not shipped), else Dark.
- Dark and Custom both recolour; the run-time colours are the defaults unless the player's Custom pack is in use.
"""
import importlib.util
import json
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
checks = failures = 0


def check(ok, why):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print("FAIL", why)


def text(rel):
    p = root / rel
    return p.read_text(encoding="utf-8-sig") if p.is_file() else ""


def floats(block):
    return [float(x) for x in re.findall(r"-?\d+(?:\.\d+)?(?=f)", block)]


# --- the enum and the palettes ---
theme = text("src/core/UiTheme.h")
check(re.search(r"enum class Preset\s*:\s*int\s*\{\s*Classic\s*=\s*0,\s*Dark\s*=\s*1,\s*Custom\s*=\s*2\s*\}", theme) is not None,
      "Preset is { Classic = 0, Dark = 1, Custom = 2 }")
check("Light" not in theme, "UiTheme.h has no Light preset or palette left")
classic = theme.split("kClassic{", 1)[1] if "kClassic{" in theme else ""
search = floats(classic.split("},\n\t\t{", 1)[0]) if classic else []
LIGHT_SEARCH = [0.82, 0.77, 0.68, 1.0, 0.24, 0.17, 0.10, 1.0, 0.78, 0.56, 0.22, 1.0, 0.12, 0.08, 0.045, 1.0,
                0.34, 0.27, 0.19, 1.0, 0.40, 0.22, 0.06, 1.0, 0.0585, 0.0313, 0.0235, 1.0]
check(search == LIGHT_SEARCH, f"Classic's search bar takes Light's colours (got {search[:8]}...)")
check(re.search(r"Recolors\(Preset a_preset\)\s*\{\s*return a_preset != Preset::Classic;", theme) is not None,
      "Dark and Custom both recolour (Recolors: not Classic)")

# --- the bars: no PNGs ---
bar = text("src/search/SearchBar.cpp")
check("SearchBar.png" not in bar and "BarTexture(" not in bar and "SetBrushFromTexture" not in bar,
      "SearchBar.cpp no longer loads the SearchBar PNGs")
check(not list((root / "assets/Search").glob("*.png")), "assets/Search holds no bar PNGs")

# --- the shipped Dark pack and the zip ---
dark = root / "assets/DarkPack"
check(all((dark / f"MRDark_P.{e}").is_file() for e in ("pak", "utoc", "ucas", "json")), "assets/DarkPack holds MRDark_P.pak/.utoc/.ucas/.json")
try:
    manifest = json.loads((dark / "MRDark_P.json").read_text(encoding="utf-8"))
except (OSError, ValueError):
    manifest = {}
check(manifest.get("pack") == "MRDark_P", "the shipped Dark pack was built under the name MRDark_P")
check(not (root / "assets/CustomPack").exists(), "assets/CustomPack is gone (the player's pack is never shipped)")
spec = importlib.util.spec_from_file_location("plugin_files", root / "tools/plugin_files.py")
plugin_files = importlib.util.module_from_spec(spec)
spec.loader.exec_module(plugin_files)
entries = plugin_files.entries(root)
prefix = plugin_files.PREFIX + "MenusRevamped/"
for e in ("pak", "utoc", "ucas", "json"):
    check(entries.get(prefix + f"DarkPack/MRDark_P.{e}") == dark / f"MRDark_P.{e}", f"the zip ships DarkPack/MRDark_P.{e}")
check(not any(n.startswith(prefix + "CustomPack/") for n in entries), "the zip ships nothing in MenusRevamped/CustomPack")
check(not any(n.startswith(prefix + "Search/") for n in entries), "the zip ships no Search bar PNGs")

# --- the plugin side ---
pack = text("src/core/DarkPack.cpp")
check("presetpacks::Choose(" in pack and "presetpacks::Switch(" in pack, "DarkPack.cpp chooses and switches through presetpacks")
check('L"DarkPack"' in pack and 'L"MRDark_P.json"' in pack, "DarkPack.cpp reads the shipped pack from Plugins/MenusRevamped/DarkPack")
check("darkstyle::kDefaults" in pack, "DarkPack.cpp falls back to the default colours when the player's pack is not in use")

# --- texts: no Light left ---
ini = text("config/MenusRevamped.defaults.ini")
style = ini.split("[Style]", 1)[1].split("uPreset", 1)[0] if "[Style]" in ini else ""
check("0 = Classic" in style and "1 = Dark" in style and "2 = Custom" in style, "the INI comment lists 0 Classic, 1 Dark, 2 Custom")
check("Light" not in style and "SearchBar" not in style, "the INI comment no longer mentions Light or the SearchBar PNGs")
check("MRDark_P" in style and "MRCustom_P" in style, "the INI comment names both packs")
readme = text("README.md")
check("Classic or Light" not in readme and "1 = Light" not in readme, "README no longer mentions the Light preset")
check("MRDark_P" in readme and "DarkPack" in readme, "README explains the shipped Dark pack")
for rel in ("tools/pack_builder/pipeline.py", "tools/pack_builder/app.py", "tools/pack_builder/window.py", "tools/plugin_files.py"):
    check("Classic/Light" not in text(rel) and "or Light" not in text(rel), f"{rel} no longer mentions the Light preset")

print(f"{checks} checks, {failures} failures")
sys.exit(1 if failures else 0)
