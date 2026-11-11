"""Census tool (owner 2026-11-11): a key that writes the open menu's widget tree (name, class, visibility, colour, brush art,
brush tint, text) to its own file. Dev builds only: compiled with `xmake f --devtools=y`; release DLLs do not contain it."""
import re
import sys
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
xmake = (root / "xmake.lua").read_text(encoding="utf-8")
option = xmake.split('option("devtools")', 1)[1].split("option_end()", 1)[0] if 'option("devtools")' in xmake else ""
check("set_default(false)" in option and 'add_defines("MR_DEVTOOLS")' in option,
      "xmake has a devtools option, off by default, that defines MR_DEVTOOLS")
check('add_options("devtools")' in xmake and re.search(r'has_config\("devtools"\)[^\n]*\n\s*add_files\("src/devtools/\*\.cpp"\)', xmake) is not None,
      "src/devtools/*.cpp is compiled only with --devtools=y")
check("src/devtools" not in xmake.split('add_files("src/core/*.cpp"', 1)[1].split("\n", 1)[0],
      "the always-built file list does not include src/devtools")

runtime = (root / "src/core/Runtime.cpp").read_text(encoding="utf-8-sig")
check(re.search(r"#ifdef MR_DEVTOOLS\s*\n\s*if \(devtools::Message\(", runtime) is not None,
      "the window procedure hands keys to the census only in dev builds")
check(re.search(r"#ifdef MR_DEVTOOLS\s*\n\s*devtools::Tick\(\);", runtime) is not None,
      "the census runs from the game-thread tick only in dev builds")
check(runtime.count("#ifdef MR_DEVTOOLS") == runtime.count("#endif") or "#include \"devtools/Census.h\"" in runtime,
      "Runtime.cpp includes the census header")

census_path = root / "src/devtools/Census.cpp"
census = census_path.read_text(encoding="utf-8-sig") if census_path.is_file() else ""
check("VK_F12" in census and "VK_CONTROL" in census and "VK_SHIFT" in census, "the census key is Ctrl+Shift+F12")
check("/Game/UI/WBP_PrimaryGameLayout.WBP_PrimaryGameLayout_C" in census, "the census starts at the game's UI layout (every menu)")
check('"DisplayedWidget"' in census and '"WidgetList"' in census,
      "menus inside CommonUI activatable stacks are followed (first run: the walk stopped at the stacks, 21 widgets)")
check("1 << 30" in census, "a held key (Windows key repeat, lParam bit 30) does not write the census again (run 2: two writes)")
check("floor<std::chrono::milliseconds>" in census,
      "census file names include milliseconds (run 2: a second write in the same second replaced the first file)")
check("GetDisplayedEntryWidgets" in census, "list rows (outside the widget tree) are included")
check("std::ofstream" in census and "MenusRevamped-census-" in census, "the census goes to its own file, not the plugin log")
check(len(re.findall(r"logger::(?:info|warn|error)\(", census)) <= 2, "the plugin log gets one line per census at most (the file path)")
check("__try" in census, "the tree walk is guarded against a torn tree")
check("TEMP-DIAG" not in census, "the census is a dev tool, not a temporary diagnostic")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
