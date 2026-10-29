"""Execute the wheel's real lookup bodies against our export names (no legacy DLLs).

The keyboard calls the search directly (no SearchApi bridge since Phase 2), so the one cross-mod
boundary left is Improved Wheels Plus finding the MenusRevamped_* exports.
"""
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


plugin = source_paths.read("plugin")
keyboard = (wheel_root / "src/Keyboard.cpp").read_text(encoding="utf-8")
pad = (wheel_root / "src/Pad.cpp").read_text(encoding="utf-8")
answer = "MenusRevampedAnswers" if "bool MenusRevampedAnswers(" in keyboard else "SearchLightAnswers"
wheel = extract(keyboard, f"bool {answer}(") + "\n" + extract(pad, "bool SearchKeyboardOpen(")
prefix = r"""
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <iostream>
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
    check(WHEEL_ANSWERS("MenusRevamped_IsTyping"),"wheel recognizes unified typing export");
    check(WHEEL_ANSWERS("MenusRevamped_CursorOverBar"),"wheel recognizes unified hover export");
    check(SearchKeyboardOpen(),"wheel recognizes unified controller keyboard");
    const auto modules=moduleLookups, exports=exportLookups;
    check(SearchKeyboardOpen() && moduleLookups==modules && exportLookups==exports,
          "wheel retains cached keyboard export on repeated input reads");
    std::cout<<checks + CHECKS_PY<<" checks, "<<failed + FAILED_PY<<" failures\n";
    return failed + FAILED_PY ? 1 : 0;
}
"""
# The exports the wheel looks up must stay in the plugin's source, with these exact names.
py_checks = py_failed = 0
for name in ("MenusRevamped_IsTyping", "MenusRevamped_CursorOverBar", "MenusRevamped_IsControllerKeyboardOpen"):
    py_checks += 1
    if f"__declspec(dllexport) bool {name}()" not in plugin:
        py_failed += 1
        print(f"FAIL {name} is exported by the plugin")
cpp = out / "integration_boundaries_tests.cpp"
cpp.write_text(prefix + wheel + tests.replace("WHEEL_ANSWERS", answer)
               .replace("CHECKS_PY", str(py_checks)).replace("FAILED_PY", str(py_failed)), encoding="utf-8")
exe = out / "integration_boundaries_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp),
                f"/Fe:{exe}", f"/Fo:{out / 'integration_boundaries_tests.obj'}",
                "user32.lib"], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
