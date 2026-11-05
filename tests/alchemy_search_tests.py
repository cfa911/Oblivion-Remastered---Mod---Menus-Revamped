"""Run the real Alchemy list transform against native-list/UI doubles.

The native list is already effect-filtered. Search must only narrow it, including
zero/one-row lists; rebuilding that native list lets clearing/broadening recover.
"""
from pathlib import Path
import subprocess
import sys
import source_paths

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
src = source_paths.read('alchemy')
prefix = r'''
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include "sorting/rules/AlchemyRules.h"
#include "search/ListLifecycle.h"
#include "engine/TextUtil.h"
using namespace alchemyrules;
namespace menus { enum class Menu { kAlchemy }; }
std::string query;
namespace search { std::string GetText(menus::Menu) { return query; } }
namespace searchcfg {
struct Config { bool alchemy=true, ownFilter=true, ignoreAccents=true; } config;
const Config& Get() { return config; }
}
namespace UE { struct UObject { Ingredient ingredient; void* GetClass() { return this; } }; }
UE::UObject cache;
revamped::ListLifecycle<UE::UObject*> g_searchList;
int g_searchCache=1;
namespace reflect { int Offset(void*, const char*) { return 0; } UE::UObject* Get(int) { return &cache; } }
bool RootSearchItems(UE::UObject*, const std::vector<UE::UObject*>&) { return true; }
namespace logger {
void flush() {}
template<class... T> void warn(T&&...) {}
}
std::vector<UE::UObject*> visible;
UE::UObject* selection=nullptr;
namespace ui {
bool IsWithin(UE::UObject*,UE::UObject*) { return true; }
struct Call {
bool get;
UE::UObject* value=nullptr;
Call(UE::UObject*,const wchar_t* name): get(std::wstring_view(name)==L"BP_GetSelectedItem") {}
explicit operator bool() const { return true; }
bool Run() { if(!get) selection=value; return true; }
template<class T> T Get(const char*) { return selection; }
void Set(const char*,UE::UObject* item) { value=item; }
};
}
bool g_dirtySort=true;
SortState g_sortState;
UE::UObject* Field(UE::UObject* o,const char*) { return o; }
UE::UObject* InnerList(UE::UObject* o) { return o; }
std::vector<UE::UObject*> ListItems(UE::UObject*) { return visible; }
void SetItems(UE::UObject*, const std::vector<UE::UObject*>& items) { visible=items; selection=nullptr; }
bool ReadIngredient(const std::uint8_t* p,Ingredient& data,bool,bool effects) {
data=reinterpret_cast<const UE::UObject*>(p)->ingredient;
if(!effects) data.knownEffects.clear();
return true;
}
'''
main = r'''
int main() {
int checks=0, failed=0;
auto check=[&](bool ok,const char* why) { ++checks; if(!ok) { ++failed; std::cerr<<"FAIL "<<why<<"\n"; } };
UE::UObject menu, apple{{"Apple",4,1,2,{"Restore Health"}}}, berry{{"Berry",2,2,7,{"Damage Health"}}}, flower{{"Caf\xC3\xA9 Flower",1,0.5f,9,{"Restore Magicka"}}};
auto native=[&] { g_searchList.nativeDirty=true; visible={&apple,&berry,&flower}; selection=&berry; };
native(); query="app"; ApplySort(&menu);
check(visible==std::vector<UE::UObject*>{&apple},"partial ingredient name narrows native list");
native(); query="RESTORE"; ApplySort(&menu);
check(visible==std::vector<UE::UObject*>{&apple,&flower},"known effects match case-insensitively");
native(); query="no-match"; ApplySort(&menu);
check(visible.empty(),"no matches produces empty list");
g_searchList.nativeDirty=true; visible={&apple}; query="damage"; ApplySort(&menu);
check(visible.empty(),"single native row is filtered too");
native(); query=""; ApplySort(&menu);
check(visible.size()==3 && selection==&berry,"clearing after native rebuild restores rows and selection");
g_searchList.nativeDirty=true; visible={&berry}; query="restore"; ApplySort(&menu);
check(visible.empty(),"search never reintroduces an ingredient excluded by native filters");
native(); query="health"; g_sortState={SortKey::Value,true}; ApplySort(&menu);
check(visible==std::vector<UE::UObject*>{&berry,&apple} && selection==&berry,"matching rows retain chosen sort and surviving selection");
native(); query="cafe"; ApplySort(&menu);
check(visible==std::vector<UE::UObject*>{&flower},"existing accent-folding preference applies");
native(); searchcfg::config.ignoreAccents=false; ApplySort(&menu);
check(visible.empty(),"accent folding can be disabled");
native(); query=""; g_sortState={SortKey::Name,false}; ApplySort(&menu);
check(visible.size()==3,"empty query retains ingredients regardless of known effects");
native(); query="app"; ApplySort(&menu); query=""; ApplySort(&menu);
check(visible.size()==3,"clearing restores rows without a simulated native rebuild");
native(); query="restore"; ApplySort(&menu); query="damage"; ApplySort(&menu);
check(visible==std::vector<UE::UObject*>{&berry},"changing query searches the complete source, not previous matches");
native(); query="no-match"; ApplySort(&menu); query="app"; ApplySort(&menu);
check(visible==std::vector<UE::UObject*>{&apple},"recovering from zero results needs no native rebuild");
std::cout<<checks<<" checks, "<<failed<<" failures\n";
return failed ? 1 : 0;
}
'''
helpers = source_paths.function(src, 'bool MatchesQuery(')
cpp = out / 'alchemy_search_tests.cpp'
cpp.write_text(prefix + helpers + source_paths.function(src, 'void ApplySort(') + main, encoding='utf-8')
exe = out / 'alchemy_search_tests.exe'
subprocess.run(['cl', '/nologo', '/EHsc', '/std:c++latest', '/utf-8', f'/I{root / "src"}', str(cpp), f'/Fe:{exe}', f'/Fo:{out}/', 'user32.lib'], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
