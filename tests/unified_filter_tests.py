"""Execute the unified list owner against native-widget doubles, not a filter model."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
source = source_paths.read("search")


def extract(marker, semicolon=False):
    start = source.index(marker)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + (";" if semicolon else "")


prefix = r"""
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include "ListLifecycle.h"
using namespace std::chrono_literals;
using Clock=std::chrono::steady_clock;
struct Class {};
Class cls;
namespace UE {
struct UObject {
    int id=0,category=0; std::string name;
    bool alive=true; std::vector<UObject*> rows;
    Class* GetClass() { return &cls; }
};
struct UFunction { std::string name; };
}
using Rows=std::vector<UE::UObject*>;
UE::UObject owners[6], lists[6], caches[6];
int active=1, writes=0;
bool searchOn=true, ownFilter=true;
std::string queries[6];
namespace menus {
enum class Menu { kNone,kInventory,kMagic,kContainerYours,kContainerTheirs,kQuests };
Menu Active() { return static_cast<Menu>(active); }
UE::UObject* Widget(Menu m) { return &owners[int(m)]; }
}
namespace container {
bool Open() { return active==0; }
UE::UObject* SideContent(menus::Menu m) { return &owners[int(m)]; }
}
namespace quests { UE::UObject* ListWrapper() { return &lists[5]; } }
namespace reflect {
struct Handle { UE::UObject* p=nullptr; };
bool Ok() { return true; }
Handle Hold(UE::UObject* p) { return {p}; }
UE::UObject* Get(Handle h) { return h.p && h.p->alive ? h.p : nullptr; }
}
namespace ui {
class Call {
    UE::UObject* obj; std::wstring name;
public:
    Call(UE::UObject* o,const wchar_t* n):obj(o),name(n) {}
    bool Run() { if(obj && name==L"ClearListItems") obj->rows.clear(); return bool(obj); }
};
void CallFirst(UE::UObject* obj,const wchar_t*,void* item,std::size_t) {
    obj->rows.push_back(*static_cast<UE::UObject**>(item));
}
UE::UObject* FindInTree(UE::UObject* owner,const char*) { return &lists[owner->id]; }
UE::UObject* AddToCanvas(UE::UObject*,const wchar_t*,const wchar_t* name,UE::UObject**) {
    return &caches[std::wstring(name).back()-L'0'];
}
void Visible(UE::UObject*,bool) {}
}
namespace pe {
std::string FunctionName(UE::UFunction* f) { return f->name; }
template<class T> bool WatchPost(Class*,T) { return true; }
}
namespace searchcfg {
struct Settings { bool ownFilter=true; } config;
const Settings& Get() { config.ownFilter=ownFilter; return config; }
}
namespace sorting {
int category[6]{};
std::uint64_t observed[6]{};
Rows results[6];
const Rows& Evaluate(int menu,const Rows& source,std::uint64_t generation) {
    observed[menu]=generation; auto& out=results[menu]; out.clear();
    for(auto* row:source) if(!category[menu] || row->category==category[menu]) out.push_back(row);
    return out;
}
}
namespace search {
"""
middle = r"""
FilterState g_filter[6];
bool g_filtering=false;
void RegisterMenuHooks() {}
bool IsSide(menus::Menu m) { return int(m)==3 || int(m)==4; }
bool Present(menus::Menu m,menus::Menu context) {
    return m==context || (active==0 && IsSide(m)) || (active==5 && int(m)==5);
}
bool SideShownNow(menus::Menu) { return true; }
bool MenuEnabled(menus::Menu) { return searchOn; }
UE::UObject* BarCanvasFor(menus::Menu menu,bool) { return &owners[int(menu)]; }
UE::UObject* ListViewOf(UE::UObject* p) { return p; }
int ItemCount(UE::UObject* p) { return int(p->rows.size()); }
Rows ListItemsOf(UE::UObject* p) { return p->rows; }
std::string QueryOf(menus::Menu m) { return queries[int(m)]; }
std::string Lower(std::string text) { return text; }
std::string ItemName(UE::UObject* item,menus::Menu) { return item->name; }
bool Matches(UE::UObject* item,menus::Menu,const std::string& query) {
    return item->name.find(query)!=std::string::npos;
}
void ApplyItems(UE::UObject* list,const Rows& rows) { ++writes; list->rows=rows; }
"""
tests = r"""
}
int main() {
    int checks=0,failed=0;
    const auto check=[&](bool ok,const char* why) {
        ++checks; if(!ok) { ++failed; std::cerr<<"FAIL "<<why<<"\n"; }
    };
    for(int i=0;i<6;++i) owners[i].id=lists[i].id=caches[i].id=i;
    UE::UObject sword,armor,steel;
    sword.name="iron sword"; sword.category=1;
    armor.name="iron armor"; armor.category=2;
    steel.name="steel sword"; steel.category=1;
    auto tick=[](int id) { search::FilterTick(static_cast<menus::Menu>(id),Clock::now()+1s); };
    lists[1].rows={&sword,&armor,&steel};
    sorting::category[1]=1; queries[1]="iron"; tick(1);
    check(lists[1].rows==Rows{&sword},"search and category are combined by one list owner");
    queries[1]=""; tick(1);
    check(lists[1].rows==Rows({&sword,&steel}),"clearing text keeps category and restores source rows");
    sorting::category[1]=0; tick(1);
    check(lists[1].rows.size()==3,"reset All restores complete source");
    queries[1]="missing"; tick(1);
    check(lists[1].rows.empty(),"zero matches are allowed");
    queries[1]=""; tick(1);
    check(lists[1].rows.size()==3,"zero-match result never narrows source snapshot");
    const auto written=writes; tick(1);
    check(writes==written,"unchanged result does not rewrite visible list");
    searchOn=false; sorting::category[1]=2; queries[1]="sword"; tick(1);
    check(lists[1].rows==Rows{&armor},"disabled search does not disable category filtering");
    searchOn=true; ownFilter=false; tick(1);
    check(lists[1].rows==Rows{&armor},"disabled text filter does not disable categories");
    ownFilter=true; sorting::category[1]=0; queries[1]="iron";
    lists[1].rows={&sword,&armor,&steel};
    UE::UFunction nativeAdd{"AddItem"};
    search::OnListPost(&lists[1],&nativeAdd,nullptr); tick(1);
    const auto previous=sorting::observed[1];
    sword.name="steel sword"; steel.name="iron axe";
    lists[1].rows={&sword,&armor,&steel};
    search::OnListPost(&lists[1],&nativeAdd,nullptr); tick(1);
    check(sorting::observed[1]>previous,"same-count reused-wrapper native refresh advances classification generation");
    check(lists[1].rows==Rows({&armor,&steel}),"reused wrapper names are searched again");
    active=0; queries[3]="iron"; queries[4]="steel";
    lists[3].rows={&armor,&steel}; lists[4].rows={&sword,&armor};
    tick(3); tick(4);
    check(lists[3].rows==Rows({&armor,&steel}) && lists[4].rows==Rows{&sword},
          "container queries and sources are independent");
    queries[4]=""; tick(4);
    check(lists[4].rows.size()==2 && lists[3].rows.size()==2,"clearing merchant restores only merchant source");
    lists[4].rows.clear(); search::OnListPost(&lists[4],&nativeAdd,nullptr); tick(4);
    check(lists[4].rows.empty() && search::g_filter[4].roots.empty(),"empty native sale/transfer refresh cannot resurrect removed items");
    active=1; queries[1]=""; lists[1].rows={&sword}; tick(1);
    check(!search::g_filter[3].watching && !search::g_filter[4].watching,"leaving container clears both source contexts");
    active=5; queries[5]="quest"; lists[5].rows={&sword,&armor}; tick(5);
    check(lists[5].rows.empty(),"quests use text matching without category predicate");
    sword.name=armor.name=""; lists[5].rows={&sword,&armor};
    search::OnListPost(&lists[5],&nativeAdd,nullptr); tick(5);
    check(lists[5].rows.size()==2,"unreadable quest names fail open");
    std::cout<<checks<<" checks, "<<failed<<" failures\n";
    return failed ? 1 : 0;
}
"""
cpp = out / "unified_filter_tests.cpp"
cpp.write_text(prefix + extract("struct FilterState", True) + middle
               + extract("\tstatic void OnListPost(")
               + extract("\tvoid FilterTick(") + tests, encoding="utf-8")
exe = out / "unified_filter_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp),
                *source_paths.include_flags(), f"/Fe:{exe}",
                f"/Fo:{out / 'unified_filter_tests.obj'}"], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
