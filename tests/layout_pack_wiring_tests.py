"""Custom pack builder Step B (owner 2026-11-12, decisions 44-46) wiring in the plugin (static checks):
- [Layout] bLayoutPack=1 (default) is read; the defaults file documents it next to bCompactRows;
- darkpack::Apply installs exactly one pack through layoutpack::Switch, with the layout manifest check, and logs the outcome;
- darkpack::LayoutBaked() says the sizes are baked (MRLayout_P in ~mods, or MRCustom_P carrying sizes): the run-time compact rows
  then step aside (decision 45: they are removed after the owner accepts the bake), and the Alchemy filter chips follow the tall
  parchment either way."""
import re
import sys

import source_paths

fails = []
count = 0


def check(ok, why):
    global count
    count += 1
    if not ok:
        fails.append(why)


root = source_paths.ROOT


def read(rel):
    return (root / rel).read_text(encoding="utf-8")


config_h, config_cpp = read("src/core/Config.h"), read("src/core/Config.cpp")
check(re.search(r"bool\s+layoutPack\s*=\s*true", config_h) is not None, "Config: layoutPack, on by default")
check('L"Layout", L"bLayoutPack", 1, 0, 1' in config_cpp, "Config reads [Layout] bLayoutPack (default 1)")
defaults = read("config/MenusRevamped.defaults.ini")
layout = defaults.split("[Layout]", 1)[1].split("\n[", 1)[0] if "[Layout]" in defaults else ""
check("bLayoutPack=1" in layout and "MRLayout_P" in layout, "the defaults file documents bLayoutPack=1 and MRLayout_P in [Layout]")

dark_cpp, dark_h = read("src/core/DarkPack.cpp"), read("src/core/DarkPack.h")
check('#include "core/rules/LayoutPack.h"' in dark_cpp and "layoutpack::Switch(" in dark_cpp, "darkpack::Apply switches through layoutpack::Switch")
check("MRLayout_P.json" in dark_cpp and "layoutpack::LayoutManifestOk(" in dark_cpp, "the layout manifest's game build is checked")
check("mrconfig::Get().layoutPack" in dark_cpp, "bLayoutPack decides whether the layout pack may be installed")
check("darkpackswitch::SwitchCustom(" not in dark_cpp, "the colour pack is no longer switched on its own (never both packs)")
check("Menus Revamped: sizes pack" in dark_cpp, "the layout pack outcome is logged")
check("bool LayoutBaked()" in dark_h, "darkpack::LayoutBaked() is declared")

# decision 45 (owner accepted the baked sizes 2026-11-12): the run-time compact rows and [Layout] bCompactRows are gone
check(not (root / "src/layout/CompactRows.cpp").exists() and not (root / "src/layout/rules/CompactRowsTable.h").exists(),
      "the run-time compact rows code and its table are removed (the sizes live in tools/menus/layout.json)")
runtime = read("src/core/Runtime.cpp")
check("compactrows::" not in runtime and "CompactRows.h" not in runtime, "the frame tick no longer runs compact rows")
check("compactRows" not in config_h and "bCompactRows" not in config_cpp, "Config no longer reads [Layout] bCompactRows")
check("bCompactRows" not in defaults, "the defaults file no longer has bCompactRows")
alchemy = read("src/alchemy/AlchemyLayout.cpp")
check("compactRows" not in alchemy and "if (darkpack::LayoutBaked())" in alchemy,
      "the Alchemy chips follow the tall parchment when the sizes are baked")

print(f"{count} checks, {len(fails)} failures")
for why in fails:
    print("FAIL:", why)
sys.exit(1 if fails else 0)
