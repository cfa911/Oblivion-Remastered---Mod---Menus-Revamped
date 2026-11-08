"""INI options requested by the owner 2026-11-05: picker switches, toggle icons, Alchemy circle palette/size,
search hotkey, controller keyboard timings and one shared stick deadzone. Every default equals the old hard-coded value.
"""
import configparser
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

root = Path(__file__).resolve().parents[1]
src = root / "src"
checks = failures = 0


def check(ok, why):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print("FAIL", why)


def read(rel):
    return (src / rel).read_text(encoding="utf-8-sig")


ini = configparser.RawConfigParser(strict=False)
ini.optionxform = str
ini.read_string((root / "config/MenusRevamped.defaults.ini").read_text(encoding="utf-8-sig"))


def default(section, key, value):
    check(ini.has_option(section, key) and ini.get(section, key).strip() == value, f"defaults [{section}] {key}={value}")


# 1-3: pickers and toggle icons
for key in ("bInventory", "bMagic", "bContainer", "bStolenFilter", "bEnchantedFilter"):
    default("Picker", key, "1")
config = read("core/Config.h") + read("core/Config.cpp")
for key in ("bInventory", "bMagic", "bContainer", "bStolenFilter", "bEnchantedFilter"):
    check(f'L"Picker", L"{key}"' in config, f"config reads [Picker] {key}")
# Owner 2026-11-08: the order inside a category-sort group is an option, A-Z first by default.
default("Picker", "bCategorySortValueFirst", "0")
check('L"Picker", L"bCategorySortValueFirst"' in config and "categorySortValueFirst" in config,
      "config reads [Picker] bCategorySortValueFirst")
own = read("sorting/OwnSort.cpp")
check(own.count("mrconfig::Get().picker.categorySortValueFirst") >= 2,
      "Inventory/container and Magic refills pass bCategorySortValueFirst to the group order")
picker = read("sorting/Picker.cpp")
tick = picker[picker.find("void Tick(Mode a_mode"):]
check("mrconfig::Get().picker" in tick, "sorting Tick turns a switched-off menu's pickers off")
header = read("sorting/PickerHeader.cpp")
check(re.search(r"enchantedfilter::Supports\(kind\)\s*&&\s*mrconfig::Get\(\)\.picker\.enchantedFilter", header) is not None,
      "enchanted icon only created when bEnchantedFilter=1")
check(re.search(r"stolenfilter::Supports\(kind\)\s*&&\s*mrconfig::Get\(\)\.picker\.stolenFilter", header) is not None,
      "stolen icon only created when bStolenFilter=1")

# 13: Alchemy circles
default("Alchemy", "uMarkerDiameter", "18")   # owner 2026-11-05: circles 30% bigger than 14
default("Alchemy", "sMarkerColors", "#316EE0,#DC3730,#48AE37,#EFCF2F,#9740C3,#ED8623,#27C0CC,#E05AA0,#607D24,#91512C,#545F91,#F2A8A1")
markers = read("sorting/rules/AlchemyEffectMarkers.h")
check("constexpr unsigned char rgb" not in markers, "circle palette is not hard-coded inside MarkerColor")
check("kMarkerDiameter" not in markers + source_paths.read("alchemy"), "circle diameter is not a compile-time constant")
check("ParseMarkerPalette" in markers and "ParseMarkerPalette" in config, "config parses sMarkerColors")
check('L"uMarkerDiameter", 18,' in config and "markerDiameter = 18;" in config and "g_markerDiameter = 18;" in markers, "code default circle size is 18")

# 4: search hotkey
default("Search", "uHotkey", "70")
default("Search", "bHotkeyCtrl", "1")
typing = read("search/SearchInput.cpp")
check("a_w == 'F'" not in typing, "Ctrl+F is no longer hard-coded")
check("hotkey" in typing and "g_swallowChar" in typing, "hotkey without Ctrl does not type its own character")
check("MapVirtualKeyW" in typing, "a hotkey that types nothing (F3) does not eat the next typed letter")

# 5, 6: controller keyboard timings
default("SearchKeyboard", "uRepeatDelayMs", "350")
default("SearchKeyboard", "uRepeatRateMs", "110")
default("SearchKeyboard", "uClearHoldMs", "700")
keyboard = read("keyboard/Keyboard.cpp")
for literal in ("350ms", "110ms", "700ms"):
    check(literal not in keyboard, f"keyboard timing {literal} comes from the INI")
kbcfg = read("keyboard/KeyboardConfig.cpp")
for key in ("uRepeatDelayMs", "uRepeatRateMs", "uClearHoldMs"):
    check(key in kbcfg, f"keyboard config reads {key}")

# 9: one stick deadzone
default("Controller", "uStickDeadzone", "16000")
check("16000" not in keyboard and "16000" not in read("search/WorldMapSearch.cpp"), "keyboard and map use the shared deadzone")
check("constexpr int Deadzone" not in read("sorting/rules/ControllerPolicy.h"), "picker deadzone is configurable")
check('L"uStickDeadzone"' in config, "config reads uStickDeadzone")

print(f"{checks} checks, {failures} failures")
sys.exit(1 if failures else 0)
