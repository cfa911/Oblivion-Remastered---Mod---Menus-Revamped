"""Execute real keyboard/search and wheel lookup bodies without legacy DLLs."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

search_root, wheel_root, out = map(Path, sys.argv[1:4])
out.mkdir(parents=True, exist_ok=True)


def extract(source, marker):
    start = source.index(marker)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


api = source_paths.read("keyboard-api")
keyboard = (wheel_root / "src/Keyboard.cpp").read_text(encoding="utf-8")
pad = (wheel_root / "src/Pad.cpp").read_text(encoding="utf-8")
api = "\n".join(line for line in api.splitlines() if not line.startswith("#include"))
answer = "MenusRevampedAnswers" if "bool MenusRevampedAnswers(" in keyboard else "SearchLightAnswers"
wheel = extract(keyboard, f"bool {answer}(") + "\n" + extract(pad, "bool SearchKeyboardOpen(")
prefix = r"""
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <iostream>
namespace menus {
enum class Menu { kNone,kInventory,kMagic,kContainerYours,kContainerTheirs,kQuests };
Menu Active() { return Menu::kInventory; }
}
std::string query;
bool external=false;
namespace msearch {
bool Enabled(menus::Menu) { return true; }
std::string GetText(menus::Menu) { return query; }
void SetText(menus::Menu,const std::string& text) { query=text; }
void SetExternalInput(bool value) { external=value; }
int Context() { return 1; }
bool PanelOnLeft(menus::Menu) { return true; }
}
namespace logger {
template<class... T> void warn(T...) {}
template<class... T> void info(T...) {}
}
bool Yes() { return true; }
int moduleLookups=0, exportLookups=0;
HMODULE TestModule(const wchar_t* name) {
    ++moduleLookups;
    return std::wstring(name)==L"MenusRevamped.dll" ? HMODULE(1) : nullptr;
}
FARPROC TestExport(HMODULE,const char* name) {
    ++exportLookups;
    return std::string(name).starts_with("MenusRevamped_") ?
        reinterpret_cast<FARPROC>(&Yes) : nullptr;
}
#define GetModuleHandleW TestModule
#define GetProcAddress TestExport
"""
tests = r"""
int main() {
    int checks=0,failed=0;
    const auto check=[&](bool good,const char* why) {
        ++checks; if(!good) { ++failed; std::cerr<<"FAIL "<<why<<"\n"; }
    };
    check(search::Present(),"compiled-in search has no separate DLL prerequisite");
    check(search::Enabled(menus::Menu::kInventory),"keyboard sees internal search enabled");
    search::SetText(menus::Menu::kInventory,"iron");
    check(query=="iron","keyboard writes directly to internal search");
    check(search::GetText(menus::Menu::kInventory)=="iron","keyboard reads internal query");
    search::SetExternalInput(true);
    check(external,"keyboard ownership reaches internal search");
    check(search::Context()==menus::Menu::kInventory,"internal active context");
    check(search::PanelOnLeft(menus::Menu::kInventory),"internal panel side");
    check(WHEEL_ANSWERS("MenusRevamped_IsTyping"),"wheel recognizes unified typing export");
    check(WHEEL_ANSWERS("MenusRevamped_CursorOverBar"),"wheel recognizes unified hover export");
    check(SearchKeyboardOpen(),"wheel recognizes unified controller keyboard");
    const auto modules=moduleLookups, exports=exportLookups;
    check(SearchKeyboardOpen() && moduleLookups==modules && exportLookups==exports,
          "wheel retains cached keyboard export on repeated input reads");
    std::cout<<checks<<" checks, "<<failed<<" failures\n";
    return failed ? 1 : 0;
}
"""
cpp = out / "integration_boundaries_tests.cpp"
cpp.write_text(prefix + api + wheel + tests.replace("WHEEL_ANSWERS", answer), encoding="utf-8")
exe = out / "integration_boundaries_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp),
                f"/Fe:{exe}", f"/Fo:{out / 'integration_boundaries_tests.obj'}",
                "user32.lib"], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
