"""Real bar texture routing plus the active/hover constants."""
from pathlib import Path
import re,subprocess,sys
import source_paths
root=Path(__file__).resolve().parents[1];out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
prefix=r'''
#include <filesystem>
#include <iostream>
namespace menus { enum class Menu { kInventory,kContainerYours,kContainerTheirs,kAlchemy,kAlchemyFilters }; }
namespace UE { struct UObject { std::wstring file; }; }
namespace reflect { using Handle=UE::UObject*; UE::UObject* Get(Handle h){return h;} Handle Hold(UE::UObject* p){return p;} }
namespace mrconfig { std::filesystem::path PluginFolder(){return "F:/unused";} }
#include "core/UiTheme.h"
namespace mrconfig { struct Values { uistyle::Preset stylePreset=uistyle::Preset::Classic; }; const Values& Get(){static Values values;return values;} }
namespace ui { UE::UObject* ImportTexture(const std::filesystem::path& p){return new UE::UObject{p.filename().wstring()};} }
namespace logger { template<class...T> void warn(T&&...){} template<class...T>void debug(T&&...){} }
'''
src=source_paths.read('alchemy')
colors=r'''
constexpr auto& kActiveTint=uistyle::kClassic.alchemy.activeSort;
constexpr auto& kFocusTint=uistyle::kClassic.alchemy.focusTint;
constexpr auto& kBarInk=uistyle::kClassic.search.ink;
'''
barsrc=source_paths.read('search')
ink=source_paths.function(barsrc,'const float* BarTextInk(')
if not ink:
    expression=re.search(r'uix::Ink\(label, (.*?)\);',barsrc).group(1)
    ink='const float* BarTextInk(menus::Menu menu,bool hint,bool sel){return '+expression+';}'
main=r'''
int main(){int checks=0,failed=0;auto check=[&](bool ok,const char* why){++checks;if(!ok){++failed;std::cerr<<"FAIL "<<why<<"\n";}};
auto* base=BarTexture(menus::Menu::kInventory);
auto* ingredient=BarTexture(menus::Menu::kAlchemy);
auto* filters=BarTexture(menus::Menu::kAlchemyFilters);
check(base->file==L"SearchBar.png","other menus keep existing artwork");
check(ingredient->file==L"SearchBarAlchemy.png","Alchemy uses owner-supplied artwork");
check(filters->file==L"SearchBarAlchemy.png","filter panel shares Alchemy artwork");
check(kActiveTint[0]>kActiveTint[1] && kActiveTint[1]>kActiveTint[2] && kActiveTint[0]<0.7f,"active sort is brown");
check(kFocusTint[0]==1 && kFocusTint[1]==1 && kFocusTint[2]==1 && kFocusTint[3]==0.55f,"hover/focus stays unchanged");
const auto dark=[](const float* c){return c[0]<0.4f && c[0]>c[1] && c[1]>c[2];};
check(dark(BarTextInk(menus::Menu::kAlchemy,false,false)),"ingredient search text is dark brown");
check(dark(BarTextInk(menus::Menu::kAlchemyFilters,true,false)),"effect-search placeholder is dark brown");
check(dark(BarTextInk(menus::Menu::kAlchemyFilters,false,true)),"selected effect-search text stays dark brown");
check(BarTextInk(menus::Menu::kInventory,false,false)==kBarInk.data(),"other menus retain existing text colors");
std::cout<<checks<<" checks, "<<failed<<" failures\n";return failed?1:0;}
'''
cpp=out/'alchemy_bar_assets_tests.cpp';cpp.write_text(prefix+colors+ink+source_paths.function(barsrc,'UE::UObject* BarTexture(')+main)
exe=out/'alchemy_bar_assets_tests.exe'
subprocess.run(['cl','/nologo','/EHsc','/std:c++latest',*source_paths.include_flags(),str(cpp),f'/Fe:{exe}',f'/Fo:{out}/'],check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
