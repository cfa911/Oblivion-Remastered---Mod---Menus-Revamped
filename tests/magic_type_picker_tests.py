"""Production picker routing and membership for Magic All/Spells/Scrolls."""
from pathlib import Path
import subprocess
import sys
import source_paths

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
prefix = r'''
#include "sorting/rules/ItemTags.h"
#include "sorting/rules/MagicRows.h"
#include <vector>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string_view>
namespace UE { struct UObject {}; }
namespace container { void SetTransferHandlers(void(*)(),void(*)()) {} }
namespace sorting {
enum class Mode { None, Inventory, Magic, Container };
namespace detail {
constexpr auto kPassAll=magicrows::kPassAll;
struct Picker {
    int kind=-1; std::atomic<bool> active{false}; unsigned category=0,choice=0;
    itemtags::Tab tab=itemtags::Tab::MagicSchool;
    std::vector<int> ticks;
    unsigned Shift() const { return kind==2 ? 32u : 0u; }
    bool Passes(std::uint64_t) const;
    void Tick(int k,UE::UObject*,UE::UObject*,float) { kind=k; ticks.push_back(k); }
} g_pickers[5];
void OnTransferStarted() {} void OnTransferSettled() {} void UpdateNativeBackgroundMask() {}
}
using namespace detail;
'''
passes = source_paths.function((root/'src/sorting/PickerTick.cpp').read_text(), 'bool Picker::Passes(')
tick = source_paths.function((root/'src/sorting/Picker.cpp').read_text(), '\tvoid Tick(')
main = r'''
}
void Check(bool ok,const char* label) { if(!ok) { std::fprintf(stderr,"FAIL %s\n",label); std::exit(1); } }
int main() {
    using namespace sorting;
    UE::UObject menu,canvas;
    Tick(Mode::Magic,&menu,&canvas,1,nullptr);
    Check(g_pickers[4].kind==5,"Magic activates third picker");
    Tick(Mode::Inventory,&menu,&canvas,1,nullptr);
    Check(g_pickers[4].kind==-1,"third picker deactivated outside Magic");
    Tick(Mode::Magic,&menu,&canvas,1,nullptr);
    Tick(Mode::Container,&menu,&canvas,1,&menu);
    Check(g_pickers[4].kind==-1,"container never owns Magic type picker");
    auto& picker=g_pickers[4]; picker.kind=5; picker.choice=1;
    Check(picker.Passes(1ull<<29),"Spells keeps spell tag");
    Check(!picker.Passes(1ull<<30),"Spells excludes scroll tag");
    picker.choice=2;
    Check(picker.Passes((1ull<<30)|magicrows::kPassAll),"known scroll with unreadable effects still has type");
    Check(!picker.Passes((1ull<<29)|magicrows::kPassAll),"known spell cannot bypass Scrolls via unreadable effects");
    Check(picker.Passes(magicrows::kPassAll),"unknown row type fails open");
    picker.choice=0;
    Check(picker.Passes(1ull<<29)&&picker.Passes(1ull<<30),"All accepts both kinds");
    const auto& info=itemtags::Info(itemtags::Tab::MagicType);
    Check(info.count==4 && std::string_view(info.names[3])=="Powers","Powers moved into Magic type picker");
    Check(itemtags::Info(itemtags::Tab::MagicSchool).count==7,"School contains All plus six schools");
    picker.choice=1;
    Check(!picker.Passes(1ull<<28),"Spells excludes powers");
    picker.choice=3;
    Check(picker.Passes(1ull<<28),"Powers includes power type");
    Check(!picker.Passes(1ull<<29)&&!picker.Passes(1ull<<30),"Powers excludes ordinary spells and scrolls");
    Check(std::wstring_view(info.icons[0])==L"MagicType-All.png","All uses supplied icon");
    Check(std::wstring_view(info.icons[1])==L"MagicType-Spells.png","Spells uses third supplied icon");
    Check(std::wstring_view(info.icons[2])==L"MagicType-Scrolls.png","Scrolls uses second supplied icon");
    std::puts("17 Magic type picker checks passed");
}
'''
cpp=out/'magic_type_picker.cpp'
exe=out/'magic_type_picker.exe'
cpp.write_text(prefix+'namespace detail {\n'+passes+'\n}\n'+tick+main)
subprocess.run(['cl','/nologo','/EHsc','/std:c++latest',*source_paths.include_flags(),str(cpp),f'/Fe{exe}',f'/Fo{out}\\'],check=True)
subprocess.run([str(exe)],check=True)
