"""RMI-047 Inventory and container/barter: RefreshInventoryHeaders / RefreshContainerHeaders (menu post-events) follow a
native tab change and re-hides the game's own sort buttons in the same event, and does no widget work otherwise."""
from pathlib import Path
import re
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
source=source_paths.read('picker')
own=source_paths.read('own-sort')
prefix=r'''
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
namespace UE {
struct UClass {};
struct UObject { UClass* cls=nullptr; UObject* vm=nullptr; std::uint8_t page=0, page2=0; bool visible=true; int writes=0; UClass* GetClass(){return cls;} };
}
namespace reflect {
using Handle=UE::UObject*; UE::UObject* Get(Handle h){return h;}
int Offset(UE::UClass*,const char* n){return std::strcmp(n,"CurrentPageInventory")==0?offsetof(UE::UObject,page):
    std::strcmp(n,"CurrentPageContainer")==0?offsetof(UE::UObject,page2):offsetof(UE::UObject,vm);}
template<class T>T* At(void* p,int offset){return p&&offset>=0?reinterpret_cast<T*>(static_cast<std::uint8_t*>(p)+offset):nullptr;}
}
namespace logger { template<class... T>void warn(T&&...){} }
UE::UObject* g_container=nullptr;UE::UObject* g_sides[2]{};
namespace container {
UE::UObject* Widget(){return g_container;}
UE::UObject* SideContent(int s){return g_container&&(s==3||s==4)?g_sides[s-3]:nullptr;}
}
namespace itemtags {
enum class Tab { Weapons, Gear, Potions, Misc, AllItems };
bool TabFromPage(int a_page,Tab& a_tab){if(a_page<0||a_page>4)return false;a_tab=static_cast<Tab>(a_page);return true;}
}
namespace ui {
struct Call {
    UE::UObject* object;std::wstring name;std::uint8_t value=0;
    Call(UE::UObject* o,const wchar_t* n):object(o),name(n){}
    explicit operator bool() const {return object!=nullptr;}
    int Size(const char*){return 1;}
    template<class T>T Get(const char*){return static_cast<T>(object->visible);}
    bool Set(const char*,std::uint8_t v){value=v;return true;}
    bool Run(){if(!object)return false;if(name==L"SetVisibility"){object->visible=value!=1;++object->writes;}return true;}
};
}
namespace sorting {
namespace detail {
struct Picker {
    int kind=-1;std::atomic<bool> active{false};reflect::Handle menu=nullptr,header=nullptr;
    itemtags::Tab tab=itemtags::Tab::Gear;
    std::vector<reflect::Handle> nativeSorts;
    int ownShows=0,sortPaints=0,tabShows=0;
    bool OwnSorts() const { return kind == 0 || kind == 1 || kind == 3 || kind == 4 || kind == 6; }
    @SORTS_RET@ HideNativeSorts();
    void ShowOwnWidgets(){++ownShows;}
    void PaintSorts(){if(!OwnSorts())return;++sortPaints;}
    void ShowTab(itemtags::Tab a_tab){++tabShows;tab=a_tab;ShowOwnWidgets();PaintSorts();}   // production: Reset + show + paint
} g_pickers[5];
'''
sortsRet=re.search(r'\t(\w+) Picker::HideNativeSorts\(',own).group(1)
prefix=prefix.replace('@SORTS_RET@',sortsRet)
sorts=sortsRet+' '+source_paths.function(own,'Picker::HideNativeSorts(')
refresh=(source_paths.function(source,'\t\tint ContainerPage(')+'\n} using namespace detail;\n'
         +source_paths.function(source,'\tstatic void RefreshTab(')+'\n'
         +source_paths.function(source,'\tvoid RefreshInventoryHeaders(')+'\n'
         +source_paths.function(source,'\tvoid RefreshContainerHeaders('))
tests=r'''
}
int main(){
using namespace sorting::detail;
unsigned checks=0,failed=0;
auto check=[&](bool ok,const char* why){++checks;if(!ok){++failed;std::cout<<"FAIL "<<why<<"\n";}};
UE::UObject vm,menu,other,header,sortA,sortB;menu.vm=&vm;vm.page=1;
auto& p=g_pickers[0];p.kind=0;p.active=true;p.menu=&menu;p.header=&header;p.tab=itemtags::Tab::Gear;
p.nativeSorts={&sortA,&sortB};
sorting::RefreshInventoryHeaders(&menu);
check(!sortA.visible&&!sortB.visible,"the game's own sort buttons collapse in the Inventory post-event");
check(p.ownShows==1&&p.sortPaints==1&&p.tabShows==0,"re-shown native sorts repaint ours once, same tab");
auto writes=sortA.writes+sortB.writes;sorting::RefreshInventoryHeaders(&menu);
check(sortA.writes+sortB.writes==writes&&p.ownShows==1&&p.sortPaints==1,
      "a post-event that changes nothing does no widget work");
vm.page=0;sortA.visible=sortB.visible=true;sorting::RefreshInventoryHeaders(&menu);
check(p.tabShows==1&&p.tab==itemtags::Tab::Weapons,"a native tab change switches the picker to the new tab in the same event");
check(!sortA.visible&&!sortB.visible&&p.ownShows==2&&p.sortPaints==2,
      "a tab change hides the native sorts and shows and paints the new tab's own sorts once");
vm.page=3;sorting::RefreshInventoryHeaders(&menu);
check(p.tabShows==2&&p.tab==itemtags::Tab::Misc&&p.ownShows==3,"a tab change with the native sorts already hidden still shows the new tab");
vm.page=9;sorting::RefreshInventoryHeaders(&menu);
check(p.tabShows==2&&p.tab==itemtags::Tab::Misc,"an unknown page is left to the per-frame Tick");
vm.page=1;sorting::RefreshInventoryHeaders(&other);
check(p.tabShows==2,"a different Inventory instance changes nothing");
p.active=false;sorting::RefreshInventoryHeaders(&menu);
check(p.tabShows==2,"an inactive picker changes nothing");
p.active=true;p.kind=1;sorting::RefreshInventoryHeaders(&menu);
check(p.tabShows==2,"a Magic picker in slot 0 is not touched by the Inventory refresh");
p.kind=0;p.header=nullptr;sortA.visible=true;sorting::RefreshInventoryHeaders(&menu);
check(p.tabShows==2&&sortA.visible,"before our header exists, nothing native is hidden");
p.header=&header;menu.vm=nullptr;sorting::RefreshInventoryHeaders(&menu);
check(p.tabShows==2&&sortA.visible,"an unreadable view model fails open");
// container/barter: slot 2 = player side (kind 3, CurrentPageInventory), slot 3 = other side (kind 4, CurrentPageContainer)
UE::UObject cmenu,cvm,give,take,giveHeader,takeHeader,giveSort,takeSort;cmenu.vm=&cvm;cvm.page=1;cvm.page2=1;
g_container=&cmenu;g_sides[0]=&give;g_sides[1]=&take;
auto& g=g_pickers[2];g.kind=3;g.active=true;g.menu=&give;g.header=&giveHeader;g.tab=itemtags::Tab::Gear;g.nativeSorts={&giveSort};
auto& k=g_pickers[3];k.kind=4;k.active=true;k.menu=&take;k.header=&takeHeader;k.tab=itemtags::Tab::Gear;k.nativeSorts={&takeSort};
auto invShows=p.tabShows;
sorting::RefreshContainerHeaders(&cmenu);
check(!giveSort.visible&&!takeSort.visible&&g.sortPaints==1&&k.sortPaints==1&&g.tabShows==0&&k.tabShows==0,
      "both sides collapse re-shown native sorts and repaint once in the container post-event");
check(p.tabShows==invShows,"the container refresh leaves the Inventory picker alone");
sorting::RefreshContainerHeaders(&cmenu);
check(g.sortPaints==1&&k.sortPaints==1&&giveSort.writes==1&&takeSort.writes==1,"an unchanged container post-event does no widget work");
cvm.page2=0;takeSort.visible=true;sorting::RefreshContainerHeaders(&cmenu);
check(k.tabShows==1&&k.tab==itemtags::Tab::Weapons&&!takeSort.visible&&g.tabShows==0&&g.sortPaints==1,
      "the other side's tab change switches only that side, from its own page byte");
cvm.page=3;sorting::RefreshContainerHeaders(&cmenu);
check(g.tabShows==1&&g.tab==itemtags::Tab::Misc&&k.tabShows==1,"the player side follows its own page byte");
cvm.page=1;sorting::RefreshContainerHeaders(&menu);
check(g.tabShows==1,"an event from another menu does not touch the container pickers");
g_sides[0]=&other;sorting::RefreshContainerHeaders(&cmenu);
check(g.tabShows==1&&g.tab==itemtags::Tab::Misc,"a side whose content widget changed is left to the per-frame Tick");
g_sides[0]=&give;cvm.page=9;sorting::RefreshContainerHeaders(&cmenu);
check(g.tabShows==1&&g.tab==itemtags::Tab::Misc,"an unknown container page is left to the per-frame Tick");
cvm.page=1;g.active=false;sorting::RefreshContainerHeaders(&cmenu);
check(g.tabShows==1,"an inactive side changes nothing");
g.active=true;cmenu.vm=nullptr;sorting::RefreshContainerHeaders(&cmenu);
check(g.tabShows==1&&k.tabShows==1,"an unreadable container view model fails open");
std::cout<<checks<<" checks, "<<failed<<" failures\n";return failed?1:0;
}
'''
cpp=out/'sort_header_refresh_tests.cpp'
cpp.write_text(prefix+sorts+'\n'+refresh+tests,encoding='utf-8')
exe=out/'sort_header_refresh_tests.exe'
subprocess.run(['cl','/nologo','/EHsc','/std:c++latest',str(cpp),f'/Fe:{exe}',f'/Fo:{out / "sort_header_refresh_tests.obj"}'],check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
