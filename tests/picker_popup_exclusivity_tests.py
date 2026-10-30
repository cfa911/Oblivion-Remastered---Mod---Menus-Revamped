"""Production popup-opening command: only one picker may stay visible/input-active."""
from pathlib import Path
import subprocess
import sys
import source_paths

root=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]); out.mkdir(parents=True,exist_ok=True)
tick=(root/'src/sorting/PickerTick.cpp').read_text()
popup=(root/'src/sorting/PickerPopup.cpp').read_text()
toggle=source_paths.function(tick,'else if (cmd == pickergrid::kToggleCommand)')
opening=source_paths.function(popup,'void Picker::OpenPopup(')
if not opening:
    opening='void Picker::OpenPopup() { open=true; }'
prefix=r'''
#include "sorting/rules/ItemTags.h"
#include <mutex>
#include <cstdio>
#include <cstdint>
struct Widget { std::uint8_t visibility=0; };
namespace reflect { using Handle=Widget*; Widget* Get(Handle p){return p;} }
namespace ui { bool CallFirst(Widget* p,const wchar_t*,const void* value,unsigned) {
    if(p) p->visibility=*static_cast<const std::uint8_t*>(value); return true;
} }
struct Return { bool pending=true; void Cancel(){pending=false;} };
struct Picker {
    bool open=false,paintedOpen=false,controllerPopup=false,inputOpen=false,styled=true,commandFromController=true;
    int command=-1; unsigned choice=0,category=0,cursor=0;
    std::uint64_t nextHitMeasure=0;
    std::mutex inputLock;
    itemtags::Tab tab=itemtags::Tab::MagicSchool,view=tab;
    Return returnToList;
    Widget widget; reflect::Handle popup=&widget;
    void PaintController() {}
    unsigned Highlight() const{return choice;}
    void OpenPopup(); void Toggle();
} g_pickers[5];
'''
main=r'''
unsigned checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
unsigned OpenCount(){unsigned n=0;for(auto& p:g_pickers)n+=p.open;return n;}
int main(){
    auto& school=g_pickers[0]; auto& effect=g_pickers[1]; auto& type=g_pickers[4];
    school.open=school.paintedOpen=school.controllerPopup=school.inputOpen=true;
    school.command=7; school.choice=3; school.category=2;
    effect.Toggle();
    Check(OpenCount()==1&&effect.open,"opening Effect closes School");
    Check(!school.paintedOpen&&school.widget.visibility==1,"old popup collapses immediately");
    Check(!school.inputOpen&&!school.controllerPopup,"old popup relinquishes input");
    Check(school.command==-1&&!school.returnToList.pending,"stale command and return-to-list cancelled");
    Check(school.choice==3&&school.category==2,"applied filter selection preserved");
    effect.paintedOpen=effect.inputOpen=true; effect.choice=5;
    type.Toggle();
    Check(OpenCount()==1&&type.open&&!effect.open,"opening type closes Effect");
    Check(effect.choice==5,"Effect selection preserved");
    type.Toggle();
    Check(OpenCount()==0,"same header toggles its popup closed");
    // Container sides use the same ownership path.
    g_pickers[2].Toggle(); g_pickers[3].Toggle();
    Check(OpenCount()==1&&g_pickers[3].open,"container-side pickers are exclusive too");
    school.Toggle();
    Check(OpenCount()==1&&school.open,"opening School closes every previous owner");
    std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
'''
cpp=out/'picker_popup_exclusivity.cpp'; exe=out/'picker_popup_exclusivity.exe'
cpp.write_text(prefix+opening+'\nvoid Picker::Toggle(){int cmd=pickergrid::kToggleCommand;if(false){}'+toggle+'}\n'+main)
subprocess.run(['cl','/nologo','/EHsc','/std:c++latest',*source_paths.include_flags(),str(cpp),f'/Fe{exe}',f'/Fo{out}\\'],check=True)
subprocess.run([str(exe)],check=True)
