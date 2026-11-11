"""Compact rows (owner 2026-11-11): one INI switch, on by default, wired into the run-time tick and the build.
The values themselves are checked by compact_rows_tests.cpp; this checks the switch, the hooks and the hard rules
(no periodic object-table scan, no per-call logging, FuzzUI's assets never shipped).
"""
import configparser
import re
import sys
from pathlib import Path

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
    p = src / rel
    return p.read_text(encoding="utf-8-sig") if p.exists() else ""


ini = configparser.RawConfigParser(strict=False)
ini.optionxform = str
ini.read_string((root / "config/MenusRevamped.defaults.ini").read_text(encoding="utf-8-sig"))
check(ini.has_option("Layout", "bCompactRows") and ini.get("Layout", "bCompactRows").strip() == "1",
      "defaults [Layout] bCompactRows=1 (owner: on by default)")

config = read("core/Config.h") + read("core/Config.cpp")
check('L"Layout", L"bCompactRows", 1, 0, 1' in config, "config reads [Layout] bCompactRows, default 1")
check("compactRows = true" in read("core/Config.h"), "Values default is compact rows on")

runtime = read("core/Runtime.cpp")
check('#include "layout/CompactRows.h"' in runtime, "runtime includes the compact rows module")
tick_block = re.search(r"if \(reflect::Ok\(\)\) \{([^}]*)\}", runtime)
check(tick_block is not None and "compactrows::Tick()" in tick_block.group(1),
      "compactrows::Tick runs in the 200 ms discovery block, not every frame")

xmake = (root / "xmake.lua").read_text(encoding="utf-8")
check('"src/layout/*.cpp"' in xmake, "xmake builds src/layout")

module = read("layout/CompactRows.cpp")
check("mrconfig::Get().compactRows" in module, "the module is off when bCompactRows=0")
# Owner round 1 (2026-11-11): 21 per-class watchers filled pe's 32-entry watcher table ("pe: watcher table full" x552), so the
# Dark Magic row watch was refused and Magic lost its skin. Compact rows must take no watcher slot.
check("pe::Watch" not in module and "pe::Intercept" not in module, "compact rows use no ProcessEvent watcher (pe table is 32)")
check(module.count("reflect::Instances(") == 1, "one object-table scan per class, at the moment its watch lands (no periodic scan)")
inst = module.find("reflect::Instances(")
check(inst > 0 and "Watched" in module[max(0, inst - 600):inst],
      "the instance scan sits behind the once-per-class watch")
check("SetHeightOverride" in module and "SetWidthOverride" in module, "size boxes are set through their setters")
check("SetOffsets" in module and "SetPadding" in module, "slots are set through SetOffsets / SetPadding")
check("SetFont" in module and "SlateFontInfo" in module, "fonts change only Size, through SetFont")
check(not re.search(r"logger::(info|warn)\([^;]*\{\}[^;]*\)\s*;\s*//\s*every", module), "no per-call logging")

# Owner round 1 (2026-11-11): Inventory row shrank but its nested WBP_OriginalMenu_InventoryIconEntry kept the 48 icon (distorted
# icons, rows held at ~48); the log showed "missing(-)" for widgets the events never reached.
check("\"WidgetTree\"" in module and "\"RootWidget\"" in module and "PatchTemplate" in module,
      "each class's template widget tree is patched once, so later instances are born compact")
tick = module[module.find("void Tick()"):]
check("PatchTemplate(" in tick, "the template patch runs when the class is first found")
apply_body = module[module.find("void Apply("):module.find("void Watched(")]
check("IndexOf(" in apply_body and "nested" in apply_body,
      "a live pass also styles nested table widgets (the icon entry inside an Inventory row)")
check("anyFound" in apply_body, "a blueprint is reported only after a pass that found its widgets (no false 'missing(-)')")

# Owner round 2 (2026-11-11): Inventory icons still distorted, count/footer fonts unchanged. Log: SizeBox_60 overrides read 0,0
# (FuzzUI's 48s are two of the box's other size fields); prefab text fonts "missing(-)" in templates (the size lives on the prefab).
for field in ("MinDesiredWidth", "MinDesiredHeight", "MaxDesiredWidth", "MaxDesiredHeight"):
    check(f'"{field}"' in module, f"size boxes also match {field}")
check("PrefabFont" in module and "reflect::Fields(" in module,
      "a prefab text widget's own font property is found by reflection (templates have no inner text_block yet)")

# Owner round 3: spacers (FVector2D doubles) set through SetSize live, written in place in templates
check("Kind::Spacer" in module and 'L"SetSize"' in module, "spacers are handled and set through SetSize")

# Layout dump 2026-11-11: Magic has two slots named VerticalBoxSlot_0 (header row 0,0,0,0 and the effectiveness line 0,20,0,0);
# the last one met won, so FuzzUI's 20 -> 0 read "already" on the header slot. Every widget of a name is a candidate now.
check("candidates" in apply_body, "every widget or slot with the change's name is tried until one holds the stock value")

# before the PR (owner 2026-11-11): the owner-test diagnostics (layout dumps, per-blueprint success lines) are gone; the one-time
# "left partly stock" warning stays (bounded: once per blueprint and session)
check("TEMP-DIAG" not in module and "DumpLayout" not in module, "no temporary diagnostics in compact rows")
check("left partly stock" in module, "the one-time warning for values that could not be applied stays")

# no third-party files in the repository
assets = [p.name for p in (root / "assets").rglob("*")] if (root / "assets").exists() else []
check(not any("FuzzUI" in n or "Interface Tweaks" in n for n in assets), "no FuzzUI files in assets/")

print(f"{checks} checks, {failures} failures")
sys.exit(1 if failures else 0)
