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
uHotkey=191
bHotkeyCtrl=0
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
uRepeatDelayMs=5
uRepeatRateMs=9999
uClearHoldMs=50
[Controller]
bReturnToListAfterFilter=0
uStickDeadzone=99999
[Picker]
bMagic=0
bStolenFilter=0
[Hover]
bMagic=0
uIntervalMs=5000
uSettleMs=5000
[Guard]
uLimit=1
[Alchemy]
uDefaultSort=9
uMarkerDiameter=40
sMarkerColors=#112233,nope
""", encoding="utf-16")

main = r"""
#include "core/Config.h"
#include "sorting/rules/AlchemyEffectMarkers.h"
#include "sorting/rules/ControllerPolicy.h"
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
    check(v.search.worldMap,"map search defaults on");
    check(v.search.mapBar.x==200 && v.search.mapBar.y==78 && v.search.mapBar.w==440,"map panel fits owner red rectangle");
check(v.search.clearTextOnGameplay,"absent gameplay clear key defaults on");
check(v.keyboard.clearTextOnActivation,"absent keyboard clear key defaults on");
    check(v.keyboard.enabled && v.keyboard.button==8,"keyboard defaults");
    check(v.keyboard.holdMs==150,"hold clamps to 150 ms");
    check(v.keyboard.leftPercent==40 && v.keyboard.rightPercent==50,"right edge is at least left + 10");
    check(!v.returnToListAfterFilter,"controller option reads");
    check(v.smooth.inventoryHover && !v.smooth.magicHover,"hover switches");
    check(v.smooth.hoverIntervalMs==1000,"hover interval clamps to 1000");
    check(v.smooth.hoverSettleMs==1000,"hover settle clamps to 1000");
    check(v.smooth.objectGuardLimit==200000,"guard limit clamps to 200000");
    check(!v.smooth.metrics,"metrics default off");
    check(v.alchemy.enabled,"alchemy features default on");
    check(v.alchemy.defaultSort==4,"alchemy sort clamps to 4");
    check(v.search.hotkey==191 && !v.search.hotkeyCtrl,"search hotkey and Ctrl switch read");
    check(v.keyboard.repeatDelayMs==100 && v.keyboard.repeatRateMs==500,"keyboard repeat clamps to 100 / 500");
    check(v.keyboard.clearHoldMs==200,"hold-to-clear below 200 (but not 0) rises to 200");
    check(v.stickDeadzone==30000 && pickerpad::Deadzone==30000,"deadzone clamps to 30000 and reaches the picker rules");
    check(v.picker.inventory && !v.picker.magic && v.picker.container,"picker menu switches, absent ones default on");
    check(!v.picker.stolenFilter && v.picker.enchantedFilter,"toggle icon switches");
    check(v.alchemy.markerDiameter==20 && alchemyrules::g_markerDiameter==20,"circle size clamps to 20");
    check(alchemyrules::g_markerPalette==alchemyrules::kDefaultMarkerPalette,"bad colour list keeps the default palette");
    {
        auto p=alchemyrules::kDefaultMarkerPalette;
        check(alchemyrules::ParseMarkerPalette("#0072B2, E69F00,#56b4e9,#F0E442,#009E73,#D55E00,#CC79A7,#FFFFFF",p) && p[0]==0x0072B2 && p[2]==0x56B4E9 && p[7]==0xFFFFFF,"8 colours parse, '#' optional, any case");
        auto q=alchemyrules::kDefaultMarkerPalette;
        check(!alchemyrules::ParseMarkerPalette("#0072B2,#E69F00",q) && q==alchemyrules::kDefaultMarkerPalette,"too few colours rejected");
        check(!alchemyrules::ParseMarkerPalette("#0072B2,#E69F00,#56B4E9,#F0E442,#009E73,#D55E00,#CC79A7,#FFFFFF,#000000",q),"too many colours rejected");
        check(!alchemyrules::ParseMarkerPalette("#0072B2,#E69F00,#56B4E9,#F0E442,#009E73,#D55E00,#CC79A7,#FFFFFG",q),"non-hex rejected");
    }
const auto ini=mrconfig::PluginFolder()/L"MenusRevamped.ini";
WritePrivateProfileStringW(L"Search",L"bClearTextOnGameplay",L"0",ini.c_str());
mrconfig::Load();
check(!v.search.clearTextOnGameplay && v.keyboard.clearTextOnActivation,"gameplay clearing independently disabled");
WritePrivateProfileStringW(L"Search",L"bClearTextOnGameplay",L"1",ini.c_str());
WritePrivateProfileStringW(L"SearchKeyboard",L"bClearTextOnActivation",L"0",ini.c_str());
mrconfig::Load();
check(v.search.clearTextOnGameplay && !v.keyboard.clearTextOnActivation,"keyboard clearing independently disabled");
WritePrivateProfileStringW(L"Search",L"bWorldMap",L"0",ini.c_str());
WritePrivateProfileStringW(L"Map",L"uX",L"339.25",ini.c_str());
WritePrivateProfileStringW(L"Map",L"uWidth",L"560",ini.c_str());
mrconfig::Load();
check(!v.search.worldMap,"world map search independently disabled");
check(v.search.mapBar.x==339.25 && v.search.mapBar.w==560,"map placement reads independently");
WritePrivateProfileStringW(L"Alchemy",L"bEnabled",L"0",ini.c_str());
WritePrivateProfileStringW(L"Alchemy",L"uDefaultSort",L"2",ini.c_str());
mrconfig::Load();
check(!v.alchemy.enabled && v.alchemy.defaultSort==2,"alchemy switch and sort read");
WritePrivateProfileStringW(L"Alchemy",L"sMarkerColors",L"#0072B2,#E69F00,#56B4E9,#F0E442,#009E73,#D55E00,#CC79A7,#FFFFFF",ini.c_str());
WritePrivateProfileStringW(L"SearchKeyboard",L"uClearHoldMs",L"0",ini.c_str());
mrconfig::Load();
check(alchemyrules::g_markerPalette[1]==0xE69F00,"valid colour list replaces the palette");
check(v.keyboard.clearHoldMs==0,"hold-to-clear 0 = never");
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
