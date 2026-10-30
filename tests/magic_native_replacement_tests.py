"""Exercise the production native-header matching block for both replacements."""
from pathlib import Path
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]); out.mkdir(parents=True,exist_ok=True)
source=(root/'src/sorting/PickerHeader.cpp').read_text()
start=source.index('\t\t\tif (kind == 1 &&')
end=source.index('\t\t\tchildren.push_back(child);',start)
match=source[start:end]
code=r'''
#include <string>
#include <vector>
#include <cstdio>
struct Widget { std::string name; const Widget& GetFName() const {return *this;} std::string ToString() const {return name;} };
namespace pe { std::string Utf8(std::string s) {return s;} }
namespace reflect { using Handle=Widget*; Widget* Get(Handle h){return h;} Handle Hold(Widget* p){return p;} }
struct Picker { Widget* header=nullptr; } g_pickers[5];
bool Match(int kind,Widget* target) {
    Widget name{"Name"},school{"School"},category{"ItemCategory"},effect{"EffectValue"};
    std::vector<Widget*> children;
    int schoolAt=-1;
    reflect::Handle nativeSchool=nullptr;
    for(auto* widget : {&name,&category,&school,&effect}) {
'''+match+r'''
        children.push_back(widget);
    }
    return schoolAt==(kind==1 ? 2 : 1) && nativeSchool && nativeSchool->name==target->name;
}
int main() {
    Widget school{"School"},category{"ItemCategory"};
    if(!Match(1,&school)) {std::puts("FAIL native School replacement"); return 1;}
    if(!Match(5,&category)) {std::puts("FAIL native ItemCategory replacement at its original position"); return 1;}
    std::puts("2 native replacement checks passed");
}
'''
cpp=out/'magic_native_replacement.cpp'; exe=out/'magic_native_replacement.exe'
cpp.write_text(code)
subprocess.run(['cl','/nologo','/EHsc','/std:c++latest',str(cpp),f'/Fe{exe}',f'/Fo{out}\\'],check=True)
subprocess.run([str(exe)],check=True)
