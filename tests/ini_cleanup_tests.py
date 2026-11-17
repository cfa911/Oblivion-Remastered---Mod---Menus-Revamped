"""Release 2.0.0 INI clean-up (owner 2026-11-17): the shipped INI holds only keys the plugin uses, with current comments."""
import configparser
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
failures = 0


def check(ok, what):
    global failures
    print(("PASS " if ok else "FAIL ") + what)
    failures += 0 if ok else 1


text = (root / "config/MenusRevamped.defaults.ini").read_text(encoding="utf-8")
ini = configparser.ConfigParser(strict=False)
ini.optionxform = str
ini.read_string(text)
search_cpp = (root / "src/search/SearchConfig.cpp").read_text(encoding="utf-8")
search_h = (root / "src/search/SearchConfig.h").read_text(encoding="utf-8")

check(list(ini["Bar"].keys()) == ["uFontSize"], "[Bar] holds only uFontSize")
for key in ("uX", "uY", "uWidth", "uHeight", "uYInventory", "uYMagic"):
    check(f'L"Bar", L"{key}"' not in search_cpp, f"SearchConfig.cpp no longer reads [Bar] {key}")
for field in ("barYInventory", "barYMagic", "barX", "barW", "barH"):
    check(not re.search(r"\b" + field + r"\b", search_h + search_cpp), f"field {field} is gone")
check("Place inventoryBar{ 440, 262, 476, 36 }" in search_h, "the Inventory default is the title-row place of the INI")
for ini_section in ("Inventory", "Magic", "Quest", "Map"):
    check(set(ini[ini_section].keys()) == {"uX", "uY", "uWidth", "uHeight"}, f"[{ini_section}] keeps its four place keys")
for stale in ("fallback", "Bar-derived", "Dark Inventory", "Light", "SearchBar", ".png"):
    check(stale not in text, f"no stale comment text '{stale}'")
check("sorting" in text.splitlines()[0].lower() and "preset" in text.splitlines()[0].lower(),
      "the header names sorting and the colour presets")
alchemy_at = text.index("[Alchemy]")
check("ingredient sort icons" in text[alchemy_at:text.index("bEnabled", alchemy_at)], "the Alchemy comment sits under [Alchemy]")
check("World-map search" in text[text.index("[Map]"):], "the Map comment sits under [Map]")
check(ini["Log"]["uLogLevel"] == "3" and ini["Metrics"]["bEnabled"] == "0", "log 3, metrics off")

print(f"{failures} failures")
sys.exit(1 if failures else 0)
