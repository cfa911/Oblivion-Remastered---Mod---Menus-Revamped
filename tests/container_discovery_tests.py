"""Exercise the real container discovery body; count expensive tree searches."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
source = "\n".join(
    line for line in source_paths.read("container").splitlines()
    if not line.startswith("#include")
)
prefix = r"""
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include "TransferHold.h"
namespace UE {
struct UObject { bool live=true, shown=true; int serial=1; };
struct UClass : UObject {};
struct UFunction { std::string name; };
UClass cls;
template<class T> T* StaticFindObject(void*,void*,const wchar_t*) { return &cls; }
}
UE::UObject menu, yours, theirs, replacement;
bool anyOpen=true, ready=true;
std::uint64_t clockMs=1000;
std::uint64_t GetTickCount64() { return clockMs; }
namespace reflect {
struct Handle { UE::UObject* ptr=nullptr; int index=0; };
bool Ok() { return true; }
Handle Hold(UE::UObject* o) { return {o,o?o->serial:0}; }
UE::UObject* Get(const Handle& h) {
    return h.ptr && h.ptr->live && h.ptr->serial==h.index ? h.ptr : nullptr;
}
std::vector<UE::UObject*> Instances(UE::UClass*) { return {&menu}; }
}
namespace menus { bool AnyOpen() { return anyOpen; } }
namespace logger { template<class... T> void info(T&&...) {} }
namespace pe {
std::string FunctionName(UE::UFunction* f) { return f->name; }
template<class F> bool Watch(UE::UClass*,F) { return true; }
}
int walks=0;
UE::UObject* player=&yours;
namespace ui {
bool ShownOnScreen(UE::UObject* o) { return o && o->shown; }
UE::UObject* FindInTree(UE::UObject*,std::string_view name) {
    ++walks;
    if (!ready) return nullptr;
    return name=="cont_give_maincontent" ? player : &theirs;
}
}
"""
tests = r"""
int main() {
    int checks=0, failures=0;
    auto check=[&](bool ok,const char* name) {
        ++checks; if (!ok) { ++failures; std::cout<<"FAIL: "<<name<<"\n"; }
    };
    container::Tick();
    for (int i=0;i<1000;++i) {
        check(container::SideContent(3)==&yours,"player side identity");
        check(container::SideContent(4)==&theirs,"other side identity");
    }
    check(walks==2,"unchanged menu resolves each side only once");
    yours.live=false; player=&replacement;
    check(container::SideContent(3)==&replacement,"collected child is rediscovered");
    check(walks==3,"collected child requires one new search");
    UE::UFunction close{"BP_OnDeactivated"}, open{"BP_OnActivated"};
    container::OnEvent(&menu,&close,nullptr);
    check(container::SideContent(3)==nullptr,"closed menu exposes no cached side");
    yours.live=true; player=&yours;
    container::OnEvent(&menu,&open,nullptr);
    check(container::SideContent(3)==&yours,"same menu reopening invalidates previous side");
    ready=false;
    container::OnEvent(&menu,&close,nullptr);
    container::OnEvent(&menu,&open,nullptr);
    const int before=walks;
    for (int i=0;i<1000;++i) check(container::SideContent(4)==nullptr,"missing side fails open");
    check(walks==before+1,"missing side search is rate limited");
    ready=true; clockMs+=501;
    check(container::SideContent(4)==&theirs,"late layout is discovered after retry");
    // A slot can be reused while its pointer compares equal; weak serials must win.
    ++theirs.serial;
    const int beforeReuse=walks;
    check(container::SideContent(4)==&theirs,"recycled child is rediscovered");
    check(walks==beforeReuse+1,"equal raw pointers cannot bypass weak-handle validation");
    anyOpen=false;
    check(container::SideContent(4)==nullptr,"global menu close clears active state");
    std::cout<<checks<<" checks, "<<failures<<" failures; "<<walks<<" tree walks\n";
    return failures?1:0;
}
"""
cpp = out / "container_discovery_tests.cpp"
cpp.write_text(prefix + source + tests)
exe = out / "container_discovery_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp), *source_paths.include_flags(),
                f"/Fe:{exe}", f"/Fo:{out / 'container_discovery_tests.obj'}"],
               check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
