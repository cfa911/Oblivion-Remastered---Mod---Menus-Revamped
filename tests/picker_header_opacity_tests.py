"""Production header painting: All is half opacity, applied filter is full opacity."""
from pathlib import Path
import subprocess
import sys
import source_paths

root=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]); out.mkdir(parents=True,exist_ok=True)
body=source_paths.function((root/'src/sorting/PickerPopup.cpp').read_text(),'void Picker::PaintController(')
code=r'''
#include "sorting/rules/ItemTags.h"
#include "sorting/rules/PickerHighlight.h"
#include <cstdio>
struct Widget { float opacity=1; };
namespace reflect { using Handle=Widget*; Widget* Get(Handle p){return p;} }
namespace ui {
template<class... T> void RoundedBox(T...) {}
bool Float(Widget* p,const wchar_t*,float value){if(p)p->opacity=value;return true;}
}
constexpr float kClear[4]={};
struct Picker {
    unsigned choice=0,category=0,cursor=0;
    bool controllerFocus=false,open=false,controllerPopup=false;
    itemtags::Tab view=itemtags::Tab::MagicSchool;
    Widget image; reflect::Handle headerImage=&image,headerFrame=nullptr;
    reflect::Handle cells[itemtags::kMaxChoices]{};
    void PaintStolen() {} void PaintController();
};
'''+body+r'''
unsigned checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
int main(){
    Picker picker;
    picker.PaintController(); Check(picker.image.opacity==0.5f,"All header is 50 percent opaque");
    picker.controllerFocus=true; picker.open=true; picker.controllerPopup=true;
    picker.PaintController(); Check(picker.image.opacity==0.5f,"focus/open popup does not make All active");
    picker.choice=2;
    picker.PaintController(); Check(picker.image.opacity==1,"selected filter remains full opacity");
    picker.choice=0; picker.category=1;
    picker.PaintController(); Check(picker.image.opacity==1,"All Items -> Weapons -> All weapons stays at full opacity");
    picker.category=0;
    picker.PaintController(); Check(picker.image.opacity==0.5f,"returning to All restores half opacity");
    std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
'''
cpp=out/'picker_header_opacity.cpp'; exe=out/'picker_header_opacity.exe'
cpp.write_text(code)
subprocess.run(['cl','/nologo','/EHsc','/std:c++latest',*source_paths.include_flags(),str(cpp),f'/Fe{exe}',f'/Fo{out}\\'],check=True)
subprocess.run([str(exe)],check=True)
