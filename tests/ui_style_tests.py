"""Verify the shared UI preset selection is exposed in the real defaults and config reader."""
from configparser import RawConfigParser
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
ini = RawConfigParser()
ini.optionxform = str
ini.read(root / "config" / "MenusRevamped.defaults.ini", encoding="utf-8-sig")

checks = failures = 0


def check(ok, why):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print("FAIL", why)


check(ini.has_option("Style", "uPreset"), "style preset has an INI default")
if ini.has_option("Style", "uPreset"):
    check(ini.getint("Style", "uPreset") == 0, "classic preset remains the default")
config = (root / "src" / "core" / "Config.cpp").read_text(encoding="utf-8-sig")
check('L"Style", L"uPreset"' in config, "config reader loads the style preset")
theme = (root / "src" / "core" / "UiTheme.h").read_text(encoding="utf-8-sig")
check("kClassic" in theme and "enum class Preset" in theme, "classic palette exists")
for preset in ("Light", "Dark"):
    check(f"k{preset}" in theme and f"Preset::{preset}" in theme, f"{preset.lower()} palette exists")
for rel in ("search/SearchBar.cpp", "sorting/PickerPopup.cpp", "sorting/OwnSort.cpp", "alchemy/AlchemyLayout.cpp"):
    source = (root / "src" / rel).read_text(encoding="utf-8-sig")
    check(any(t in source for t in ("uistyle::Get", "UiPalette()", "uistyle::SearchFor(", "uistyle::PickerFor(")), f"{rel} consumes shared style tokens")
print(f"{checks} checks, {failures} failures")
sys.exit(1 if failures else 0)
