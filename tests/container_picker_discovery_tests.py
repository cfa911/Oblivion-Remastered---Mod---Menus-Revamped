"""Execute the real picker lookup block against nested native layout doubles."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
source = source_paths.read("picker")
start = source.index("\t\t\t\t\tauto find =")
end = source.index("\n\t\t\t\t\tauto* headerWidget", start)
body = source[start:end]
cpp = out / "container_picker_discovery_tests.cpp"
cpp.write_text(r"""
#include <iostream>
#include <string_view>
#include <utility>
namespace UE { struct UObject {}; }
UE::UObject side, outerFilter, nativeNavigation, scroll;
bool nativePresent=true, outerPresent=true, scrollPresent=true;
int direct=0, cached=0;
UE::UObject* Lookup(UE::UObject* owner,std::string_view name) {
    if (owner==&side && name=="FilterIconBox" && nativePresent) return &nativeNavigation;
    if (owner==&side && name=="inv_cont_filter" && outerPresent) return &outerFilter;
    // Both ancestors can find the nested ScrollBox, but only the inner
    // navigation owner implements the existing native AddChild contract.
    if ((owner==&outerFilter || owner==&nativeNavigation) && name=="ScrollBox" && scrollPresent)
        return &scroll;
    return nullptr;
}
namespace ui {
UE::UObject* FindInTree(UE::UObject* owner,std::string_view name) {
    ++direct; return Lookup(owner,name);
}
}
namespace container {
UE::UObject* FindWidget(UE::UObject* owner,std::string_view name) {
    ++cached; return Lookup(owner,name);
}
}
std::pair<UE::UObject*,UE::UObject*> Discover(int kind,UE::UObject* a_menu) {
""" + body + r"""
    return {iconBox,panel};
}
int main() {
    int checks=0, failed=0;
    auto check=[&](bool ok,const char* text) {
        ++checks; if (!ok) { ++failed; std::cout<<"FAIL: "<<text<<"\n"; }
    };
    for (int kind : {3,4}) {
        auto result=Discover(kind,&side);
        check(result.first==&nativeNavigation,"container uses inner native navigation, not outer layout");
        check(result.second==&scroll,"container resolves native sort ScrollBox");
    }
    check(cached>0 && direct==0,"container discovery stays on cached lookup path");
    auto inventory=Discover(0,&side);
    check(inventory.first==&nativeNavigation && direct>0,"Inventory discovery is unchanged");
    nativePresent=false;
    check(Discover(3,&side).first==&outerFilter,"legacy outer-only layout fallback preserved");
    outerPresent=false;
    check(Discover(4,&side).first==nullptr,"absent navigation fails open");
    nativePresent=true; scrollPresent=false;
    check(Discover(3,&side).second==nullptr,"missing panel fails open");
    std::cout<<checks<<" checks, "<<failed<<" failures\n";
    return failed?1:0;
}
""")
exe = out / "container_picker_discovery_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp),
                f"/Fe:{exe}", f"/Fo:{out / 'container_picker_discovery_tests.obj'}"],
               check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
