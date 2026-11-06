"""Run the real potion-name tick with native-text and keyboard doubles."""
from pathlib import Path
import subprocess
import sys
import source_paths

root=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]).resolve()
out.mkdir(parents=True,exist_ok=True)
prefix=r'''
#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
namespace UE { struct UObject {}; }
namespace menus { enum class Menu { kAlchemyName }; }
std::string nativeName="Old Potion",brewName="Old Potion",query;
UE::UObject menu;
UE::UObject* Widget() { return &menu; }
std::vector<std::wstring> calls;
bool enabled=true,visible=true,build=true,taking=false,modeBeforeOpen=false;
std::atomic<bool> g_canName{false},g_nameMode{false},g_nameKeyboardRequest{false};
std::string g_nameApplied;
constexpr std::uint16_t kPadName=0x40;
namespace ui {
bool ShownOnScreen(UE::UObject*) { return visible; }
struct Call {
std::wstring fn;
Call(UE::UObject*,const wchar_t* f):fn(f) {}
explicit operator bool() const { return true; }
bool Run() { calls.push_back(fn); return true; }
template<class T> T Get(const char*) { return enabled; }
};
}
namespace logger { template<class... T> void warn(T&&...) {} template<class... T> void debug(T&&...) {} }
namespace search { void SetText(menus::Menu,const std::string& s) { query=s; } std::string GetText(menus::Menu) { return query; } }
namespace keyboard {
bool Taking() { return taking; }
bool Open(menus::Menu,std::uint16_t) { modeBeforeOpen=g_nameMode.load(); taking=build; return taking; }
}
std::string ReadPotionName(UE::UObject*) { return nativeName; }
bool NameTextCall(UE::UObject* target,const wchar_t* fn,const char* param,const std::string& value,bool=false) {
    if (target == &menu) {
        if (std::wstring(fn) != L"BndEvt__WBP_ModernMenu_AlchemyMenu_Alchemy_NameBox_K2Node_ComponentBoundEvent_11_OnEditableTextChangedEvent__DelegateSignature" ||
            std::string(param) != "Text") return false;
        brewName = value;
    } else {
        calls.push_back(fn);
        nativeName = value;
    }
    return true;
}
'''
main=r'''
int main() {
int checks=0,failed=0;
auto check=[&](bool ok,const char* why) { ++checks; if(!ok) { ++failed; std::cerr<<"FAIL "<<why<<"\n"; } };
UE::UObject box;
g_nameKeyboardRequest=true; PotionNameTick(&box);
check(taking && g_nameMode && modeBeforeOpen,"name context is published before opening keyboard");
check(query=="Old Potion" && nativeName=="Old Potion","opening loads current native text without clearing it");
query="Test Potion"; calls.clear(); PotionNameTick(&box);
check(nativeName=="Test Potion" && brewName=="Old Potion","live edits preview the new text until keyboard close");
check(calls.size()==3 && calls[1]==L"SetUserInputText" && calls[2]==L"UpdateOnTextChanged","editing uses observed wrapper setter and change route");
taking=false; calls.clear(); PotionNameTick(&box);
check(!g_nameMode && nativeName=="Test Potion","closing retains entered name and releases name context");
check(calls.size()==5 && calls[3]==L"Update on Text Committed" && calls[4]==L"StopEditText","closing commits before stopping native editing");
check(brewName=="Test Potion","closing sends controller-entered text to the native brew-name handler");
brewName="sentinel";
g_nameKeyboardRequest=true; PotionNameTick(&box);
taking=false; PotionNameTick(&box);
check(brewName=="sentinel","reopening and closing unchanged does not send a new brew name");
build=false; g_nameKeyboardRequest=true; calls.clear(); PotionNameTick(&box);
check(!g_nameMode && !taking && calls.back()==L"StopEditText","failed keyboard build unwinds native editing");
visible=false; g_nameKeyboardRequest=true; calls.clear(); PotionNameTick(&box);
check(!g_nameMode && !taking && !g_canName,"hidden native name field cannot open keyboard");
std::cout<<checks<<" checks, "<<failed<<" failures\n";
return failed ? 1 : 0;
}
'''
cpp=out/'potion_name_bridge_tests.cpp'
cpp.write_text(prefix+source_paths.function(source_paths.read('alchemy'),'void PotionNameTick(')+main,encoding='utf-8')
exe=out/'potion_name_bridge_tests.exe'
subprocess.run(['cl','/nologo','/EHsc','/std:c++latest',str(cpp),f'/Fe:{exe}',f'/Fo:{out}/'],check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
