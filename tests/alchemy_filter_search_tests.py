"""Real filter-tab rebuild and search against UI doubles; recovery uses full tab source."""
from pathlib import Path
import subprocess,sys
import source_paths
root=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
src=source_paths.read('alchemy')
prefix=r'''
#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>
#include "sorting/rules/AlchemyFilterTabs.h"
#include "engine/TextUtil.h"
using FilterTab=alchemytabs::Tab;
namespace UE { struct UObject { std::string name; }; }
namespace searchcfg { struct Config { bool ignoreAccents=true; } config; const Config& Get(){ return config; } }
bool g_settingTab=false,g_effectSearchReady=false;
FilterTab g_filterTab=FilterTab::All;
int g_nextLayout=0,g_effectSearchCache=0;
std::pair<int,void*> g_filterSignature;
std::string g_effectSearchQuery;
std::vector<UE::UObject*> positive,negative,visible,g_effectSearchItems;
UE::UObject* Field(UE::UObject* o,const char*) { return o; }
UE::UObject* InnerList(UE::UObject* o){ return o; }
UE::UObject* Widget(){static UE::UObject menu;return &menu;}
void ClickTab(UE::UObject*,bool pos){ visible=pos?positive:negative; }
std::vector<UE::UObject*> ListItems(UE::UObject*){ return visible; }
void SetItems(UE::UObject*,const std::vector<UE::UObject*>& items){visible=items;}
bool EffectItemName(UE::UObject* o,std::string& name){name=o->name;return true;}
bool RootSearchSource(UE::UObject*,const std::vector<UE::UObject*>&,int&,const wchar_t*){return true;}
namespace logger { template<class... T> void warn(T&&...){} }
'''
apply=source_paths.function(src,'void ApplyEffectSearch(') or 'void ApplyEffectSearch(UE::UObject*) {}'
main=r'''
int main(){
int checks=0,failed=0;
auto check=[&](bool ok,const char* why){++checks;if(!ok){++failed;std::cerr<<"FAIL "<<why<<"\n";}};
UE::UObject filters,restore{"Restore Health"},shield{"Fire Shield"},damage{"Damage Health"},frost{"Frost Damage"};
positive={&restore,&shield};negative={&damage,&frost};
g_effectSearchQuery="health";ShowFilterTab(&filters,FilterTab::All);
check(visible.size()==2,"All searches names across both effect types");
ShowFilterTab(&filters,FilterTab::Positive);
check(visible==std::vector<UE::UObject*>{&restore},"Positive only searches its own source");
ShowFilterTab(&filters,FilterTab::Negative);
check(visible==std::vector<UE::UObject*>{&damage},"Negative only searches its own source");
g_effectSearchQuery="not-found";ApplyEffectSearch(&filters);check(visible.empty(),"no-match query produces empty list");
g_effectSearchQuery="";ApplyEffectSearch(&filters);check(visible==negative,"clearing restores full current tab without native rebuild");
g_effectSearchQuery="DAMAGE";ApplyEffectSearch(&filters);check(visible==negative,"partial effect search is case insensitive");
g_effectSearchQuery="Frost";ApplyEffectSearch(&filters);check(visible==std::vector<UE::UObject*>{&frost},"changing query uses full cached source");
g_effectSearchQuery="Fire";ShowFilterTab(&filters,FilterTab::Positive);check(visible==std::vector<UE::UObject*>{&shield},"tab switch keeps query and uses fresh tab objects");
g_effectSearchQuery="";ApplyEffectSearch(&filters);check(visible==positive,"clearing restores exact native object identities");
std::cout<<checks<<" checks, "<<failed<<" failures\n";return failed?1:0;}
'''
cpp=out/'alchemy_filter_search_tests.cpp';cpp.write_text(prefix+apply+source_paths.function(src,'void ShowFilterTab(')+main)
exe=out/'alchemy_filter_search_tests.exe'
subprocess.run(['cl','/nologo','/EHsc','/std:c++latest',f'/I{root/"src"}',str(cpp),f'/Fe:{exe}',f'/Fo:{out}/','user32.lib'],check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
