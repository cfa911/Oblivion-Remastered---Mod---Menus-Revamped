"""Production header attachment/creation: symmetric spacing and initial header paint."""
from pathlib import Path
import subprocess
import sys
import source_paths

root=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]); out.mkdir(parents=True,exist_ok=True)
source=(root/'src/sorting/PickerHeader.cpp').read_text()
append=source_paths.function(source,'if (schoolAt < 0)')
replace=source_paths.function(source,'if (static_cast<int>(i) == schoolAt)')
helper=source_paths.function(source,'const auto addHeader =')
if helper: helper+=';'
start=source.index('carousel = reflect::Hold(navigation);')
end=source.index('// InsertChildAt/ShiftChild',start)
helper_start=source.find('const auto addHeader =',start)
if helper_start>=0: end=min(end,helper_start)
created=source[start:end]
prefix=r'''
#include <array>
#include <cstring>
#include <cstdio>
struct Slot { std::array<float,4> padding{}; };
struct Widget { Slot slot; float opacity=1; };
namespace UE { using UObject=Widget; }
namespace reflect { using Handle=Widget*; Widget* Get(Handle p){return p;} Widget* Hold(Widget* p){return p;} }
namespace ui {
struct Call {
    Widget* content=nullptr;
    Call(Widget*,const wchar_t*){}
    bool Set(const char*,Widget* p){content=p;return true;}
    bool Run(){return content!=nullptr;}
    template<class T> T Get(const char*){return reinterpret_cast<T>(&content->slot);}
};
bool CallFirst(Widget* p,const wchar_t*,const void* bytes,unsigned n){if(!p||n!=16)return false;std::memcpy(reinterpret_cast<Slot*>(p)->padding.data(),bytes,n);return true;}
}
struct Picker {
    Widget image,stolen,panel; Widget* stolenHeader=&stolen,*carousel=nullptr,*headerImage=nullptr;
    unsigned choice=0,category=0;
    void PaintController(){if(headerImage)headerImage->opacity=choice||category?1.0f:0.5f;}
    bool Attach(bool replacement); bool Created();
};
'''
attach='bool Picker::Attach(bool replacement){auto* a_panel=&panel;auto* box=&image;int schoolAt=replacement?0:-1;std::size_t i=0;'+helper+append+replace+'return true;}\n'
creation='bool Picker::Created(){Widget navigationWidget;auto* navigation=&navigationWidget;struct Rollback{bool complete=false;}rollback;headerImage=&image;if(true){'+created+'return true;}\n'
main=r'''
unsigned checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
bool Spaced(const Slot& s){return s.padding==std::array<float,4>{2.5f,0,2.5f,0} && 49+s.padding[0]+s.padding[2]==54;}
int main(){
    Picker appended;
    appended.Attach(false);
    Check(Spaced(appended.image.slot),"custom content plus symmetric padding matches native 54px footprint");
    Check(Spaced(appended.stolen.slot),"stolen header shares custom spacing");
    Picker replacement; replacement.Attach(true);
    Check(Spaced(replacement.image.slot),"native-replacement custom picker has equal side padding");
    replacement.Created();
    Check(replacement.image.opacity==0.5f,"fresh All header is half opaque before any click");
    Picker selected; selected.category=1; selected.Created();
    Check(selected.image.opacity==1,"fresh Weapons category header stays fully opaque");
    std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
'''
cpp=out/'picker_header_spacing.cpp'; exe=out/'picker_header_spacing.exe'
cpp.write_text(prefix+attach+creation+main)
subprocess.run(['cl','/nologo','/EHsc','/std:c++latest',str(cpp),f'/Fe{exe}',f'/Fo{out}\\'],check=True)
subprocess.run([str(exe)],check=True)
