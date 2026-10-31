"""Compile the real INI readers (core, search, keyboard, hover) and check what they parse from MenusRevamped.ini.

Pins the parse results (defaults, clamping, decimals, sections) so the readers can be merged without a change.
"""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)

shim = out / "config_shim.h"
shim.write_text(r"""
#pragma once
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cwchar>
#include <filesystem>
#include <string>
namespace logger {
template<class... T> void info(T&&...) {}
template<class... T> void warn(T&&...) {}
template<class... T> void debug(T&&...) {}
}
""", encoding="utf-8")

# Out-of-range values clamp, decimals parse, garbage keeps the default, absent keys keep the compiled default.
(out / "MenusRevamped.ini").write_text("""[Log]
uLogLevel=9
[Search]
bMagic=0
bIme=0
[Bar]
uFontSize=200
[Inventory]
uX=300.5
uY=abc
[ContainerYours]
uXOffset=-2000
[SearchKeyboard]
uHoldMs=5
uLeftPercent=40
uRightPercent=45
[Controller]
bReturnToListAfterFilter=0
[Hover]
bMagic=0
uIntervalMs=5000
[Guard]
uLimit=1
""", encoding="utf-16")

main = r"""
#include "core/Config.h"
#include <iostream>
int main() {
    int checks=0, failed=0;
    auto check=[&](bool ok,const char* why) { ++checks; if(!ok) { ++failed; std::cout<<"FAIL "<<why<<"\n"; } };
    mrconfig::Load();
    const auto& v=mrconfig::Get();
    check(v.search.logLevel==4,"log level clamps to 4");
    check(v.search.inventory && !v.search.magic && !v.search.ime,"search switches read, absent ones default on");
    check(v.search.barFontSize==60,"font size clamps to 60");
    check(v.search.inventoryBar.x==300.5,"decimal place parses");
    check(v.search.inventoryBar.y==109.5,"unparsable value keeps the default");
    check(v.search.yoursBar.x==-1500,"offset clamps to -1500");
    check(v.search.ownFilter,"absent filter key defaults on");
check(v.search.clearTextOnGameplay,"absent gameplay clear key defaults on");
check(v.keyboard.clearTextOnActivation,"absent keyboard clear key defaults on");
    check(v.keyboard.enabled && v.keyboard.button==8,"keyboard defaults");
    check(v.keyboard.holdMs==150,"hold clamps to 150 ms");
    check(v.keyboard.leftPercent==40 && v.keyboard.rightPercent==50,"right edge is at least left + 10");
    check(!v.returnToListAfterFilter,"controller option reads");
    check(v.smooth.inventoryHover && !v.smooth.magicHover,"hover switches");
    check(v.smooth.hoverIntervalMs==1000,"hover interval clamps to 1000");
    check(v.smooth.objectGuardLimit==200000,"guard limit clamps to 200000");
    check(!v.smooth.metrics,"metrics default off");
const auto ini=mrconfig::PluginFolder()/L"MenusRevamped.ini";
WritePrivateProfileStringW(L"Search",L"bClearTextOnGameplay",L"0",ini.c_str());
mrconfig::Load();
check(!v.search.clearTextOnGameplay && v.keyboard.clearTextOnActivation,"gameplay clearing independently disabled");
WritePrivateProfileStringW(L"Search",L"bClearTextOnGameplay",L"1",ini.c_str());
WritePrivateProfileStringW(L"SearchKeyboard",L"bClearTextOnActivation",L"0",ini.c_str());
mrconfig::Load();
check(v.search.clearTextOnGameplay && !v.keyboard.clearTextOnActivation,"keyboard clearing independently disabled");
    std::cout<<checks<<" checks, "<<failed<<" failures\n";
    return failed ? 1 : 0;
}
"""
cpp = out / "config_reader_tests.cpp"
cpp.write_text(main, encoding="utf-8")
exe = out / "config_reader_tests.exe"
sources = [str(p) for unit in ("config-core", "config-search", "config-keyboard") for p in source_paths.paths(unit)]
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", "/utf-8", f"/FI{shim}", *source_paths.include_flags(),
                str(cpp), *sources, f"/Fe:{exe}", f"/Fo:{out}\\", "user32.lib"], check=True, cwd=out)
sys.exit(subprocess.run([str(exe)], cwd=out).returncode)
