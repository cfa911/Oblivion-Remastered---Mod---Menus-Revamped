"""Run the production list writer and update watcher with UI doubles."""
from pathlib import Path
import subprocess
import sys
import source_paths

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
src = source_paths.read('alchemy')
prefix = r'''
#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <iostream>
namespace UE { struct UObject {}; struct UFunction { std::string name; }; }
struct RawArray { std::uint8_t* data; std::int32_t num, max; };
namespace pe { auto FunctionName(UE::UFunction* fn) { return fn->name; } }
namespace reflect {
auto Get(UE::UObject* o){return o;}
auto Hold(UE::UObject* o){return o;}
bool IsLive(UE::UObject*){return true;}
}
UE::UObject* g_menu=nullptr;
bool Enabled(){return true;}
bool g_settingTab=false, g_effectSearchDirty=false, g_inside=false, g_open=true, g_dirtySort=false, g_dirtyCounts=false;
bool g_nativeClearRequested=false;
std::atomic<bool> g_filtersVisible=true;
int g_tabCycle=0, g_nextLayout=0;
struct { void ListBuilt(){} } g_state;
struct { bool nativeDirty=false; } g_searchList;
int clears=0, adds=0, batches=0;
bool supportsBatch=true;
std::vector<UE::UObject*> written;
namespace ui {
struct Call {
    std::wstring name;
    RawArray array{};
    Call(UE::UObject*,const wchar_t* n):name(n){}
    explicit operator bool() const {return name!=L"BP_SetListItems" || supportsBatch;}
    int Size(const char*){return sizeof(RawArray);}
    bool Set(const char*,const RawArray& a){array=a;return true;}
    bool Set(const char*,UE::UObject*){return true;}
    bool Run(){
        if(name==L"BP_SetListItems"){
            ++batches;
            if(array.num==0) written.clear();
            else {
                auto items=reinterpret_cast<UE::UObject**>(array.data);
                written.assign(items,items+array.num);
            }
        } else if(name==L"ClearListItems") ++clears;
        else if(name==L"AddItem") ++adds;
        return true;
    }
};
}
'''
main = r'''
int main(){
    UE::UObject menu, first, second;
    UE::UFunction event{"UpdateItemsList"};
    OnMenuEvent(&menu,&event,nullptr);
    int failures=0;
    auto check=[&](bool ok,const char* why){if(!ok){++failures;std::cerr<<"FAIL "<<why<<"\n";}};
    check(!g_effectSearchDirty,"effect activation leaves the open filter list alone");
    check(g_dirtySort && g_dirtyCounts && g_searchList.nativeDirty,"ingredient update still refreshes ingredient data");
    SetItems(&menu,{&second,&first});
    check(batches==1 && clears==0 && adds==0 && written==std::vector<UE::UObject*>{&second,&first},"reorder publishes one batch without an empty-list transition");
    supportsBatch=false;
    SetItems(&menu,{&first});
    check(clears==1 && adds==1,"old list implementation retains fallback");
    UE::UFunction clearKeyboard{"InpActEvt_IA_Specific_AlchemyMenu_ClearAllFilters_Keyboard_K2Node_EnhancedInputActionEvent_2"};
    UE::UFunction clearController{"InpActEvt_IA_Specific_AlchemyMenu_ClearAllFilters_K2Node_EnhancedInputActionEvent_7"};
    OnMenuEvent(&menu,&clearKeyboard,nullptr);
    check(g_nativeClearRequested,"native keyboard clear queues shared clear action");
    g_nativeClearRequested=false;
    OnMenuEvent(&menu,&clearController,nullptr);
    check(g_nativeClearRequested,"native controller clear queues shared clear action");
    std::cout<<"6 checks, "<<failures<<" failures\n";
    return failures?1:0;
}
'''
cpp = out / 'alchemy_list_refresh_tests.cpp'
cpp.write_text(prefix + source_paths.function(src, 'void SetItems(') + source_paths.function(src, 'void OnMenuEvent(') + main)
exe = out / 'alchemy_list_refresh_tests.exe'
subprocess.run(['cl','/nologo','/EHsc','/std:c++latest',str(cpp),f'/Fe:{exe}',f'/Fo:{out}/'],check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
