"""Run production enchanted paint against a captured Slate colour."""
from pathlib import Path
import subprocess,sys
import source_paths
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
source=source_paths.read('picker')
# PaintEnchanted delegates to PaintToggle (Inventory skin inversion, 2026-11-09): compile both
body=source_paths.function(source,'static void PaintToggle(')+'\n'+source_paths.function(source,'void Picker::PaintEnchanted()')
code=r'''
#include "core/UiTheme.h"
#include "sorting/rules/StolenFilter.h"
#include "sorting/rules/EnchantedFilter.h"
#include "sorting/rules/PickerHighlight.h"
#include "sorting/rules/FilterTogglePaint.h"
#include <cstdio>
struct Widget {};
namespace UE { using UObject=Widget; }
namespace reflect { using Handle=Widget*; Widget* Get(Handle p){return p;} }
namespace mrconfig { struct Values { uistyle::Preset stylePreset=uistyle::Preset::Dark; }; const Values& Get(){static Values values;return values;} }
float colour[4]{};
namespace ui {
void RoundedBox(Widget*,bool,const float* fill,const float*,float,float){for(int i=0;i<4;++i)colour[i]=fill[i];}
void Colour(Widget*,float,float,float,float){}
struct Call { Call(Widget*,const wchar_t*){} explicit operator bool()const{return true;} int Size(const char*){return 4;} bool Set(const char*,float){return true;} bool RunGuarded(){return true;} };
}
constexpr float kClear[4]={};
struct Picker { int kind=1; bool enchantedOnly=true,enchantedFocus=false; Widget* enchantedFrame=nullptr,*enchantedImage=nullptr; void PaintEnchanted(); };
'''+body+r'''
int main(){Picker p;p.PaintEnchanted();bool blue=colour[2]>colour[0] && colour[2]>colour[1] && colour[3]==0.5f;
if(!blue)std::puts("FAIL active enchanted background must be blue at half opacity");
Picker inv;inv.kind=0;inv.PaintEnchanted();bool bare=colour[3]==0;
if(!bare)std::puts("FAIL Inventory skin: an active enchanted toggle has no coloured fill");
return blue&&bare?0:1;}
'''
cpp=out/'enchanted_paint.cpp';exe=out/'enchanted_paint.exe';cpp.write_text(code)
subprocess.run(['cl','/nologo','/EHsc','/std:c++latest',*source_paths.include_flags(),str(cpp),f'/Fe{exe}',f'/Fo{out}\\'],check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
