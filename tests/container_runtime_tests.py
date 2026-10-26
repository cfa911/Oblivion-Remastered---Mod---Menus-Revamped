"""Compile the real exported FilterList body against isolated UE boundary doubles.

Run from an MSVC developer shell: python tests/container_runtime_tests.py <output-dir>
This tests dispatch only; native widget/layout/transfer behavior needs the game.
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
source = (root / "sortingoverhaul/Runtime.cpp").read_text()
start = source.index("\tbool FilterList(")
brace = source.index("{", start)
depth = 1
end = brace + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
body = source[start:end]
cpp = out / "container_runtime_tests.cpp"
cpp.write_text(r"""
#include <vector>
#include <string>
#include <iostream>
namespace UE { struct UObject {}; }
using Match = bool (*)(UE::UObject*, const char*);
bool g_inside = false;
Match g_match = nullptr;
UE::UObject item;
namespace menus { void Tick() {} }
namespace container { void Tick() {} }
namespace gearfilter {
bool Active(int = 0) { return true; }
template<class... Args> void Filter(Args&&...) {}
}
UE::UObject* Prepare(int) { return &item; }
bool Items(UE::UObject*, std::vector<UE::UObject*>&) { return true; }
bool MatchName(UE::UObject*, const std::string&) { return true; }
""" + body + r"""
int main() {
    for (int menu = 1; menu <= 4; ++menu) {
        if (!FilterList(menu, &item, "", nullptr)) {
            std::cerr << "FAIL: supported menu " << menu << " was rejected\n";
            return 1;
        }
    }
    if (FilterList(0, &item, "", nullptr) || FilterList(5, &item, "", nullptr) ||
        FilterList(3, nullptr, "", nullptr)) return 2;
    g_inside = true;
    if (FilterList(3, &item, "", nullptr)) return 3;
    std::cout << "PASS: Inventory/Magic/both container sides dispatch; invalid/null/reentry rejected\n";
}
""")
exe = out / "container_runtime_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp),
                f"/Fe:{exe}", f"/Fo:{out / 'container_runtime_tests.obj'}"], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
