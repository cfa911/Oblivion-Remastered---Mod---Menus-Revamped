"""Execute production placement/discovery with native parent and duplicate cell names."""
from pathlib import Path
import subprocess
import sys
import source_paths

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
source = (root / 'src/sorting/InventoryHeaders.cpp').read_text()
place = source_paths.function(source, 'bool Place(')
column = source_paths.function(source, 'UE::UObject* Column(')
if not column:
    column = 'UE::UObject* Column(UE::UObject* row,std::string_view field){return ui::FindInTree(row,field);}'
code = r'''
#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
namespace UE {struct UObject {UObject* parent=nullptr; std::string name; std::vector<UObject*> children; double x=0,y=0,w=44,h=40; struct Name {std::string text; std::string ToString(){return text;}}; Name GetFName(){return {name};}};}
namespace pe {std::string Utf8(std::string s){return s;}}
namespace inventoryheaders {struct Box {double x=0,y=0,w=0,h=0;};}
namespace ui {
int mutations=0;
struct Call {
 UE::UObject* obj;std::wstring fn;UE::UObject* child=nullptr;int index=0;
 Call(UE::UObject* o,const wchar_t* f):obj(o),fn(f){}
 explicit operator bool()const{return obj;}
 template<class T>bool Set(const char* n,T v){if constexpr(std::is_pointer_v<T>)child=v;else index=int(v);return true;}
 bool RunGuarded(){if(fn==L"AddChildToCanvas"){++mutations;child->parent=obj;}return obj;}
 template<class T>T Get(const char*){if constexpr(std::is_pointer_v<T>){if(fn==L"GetParent")return obj->parent;if(fn==L"GetChildAt")return obj->children[index];return obj;}else return T(obj->children.size());}
 };
 bool CallFirst(UE::UObject*,const wchar_t*,const void*,size_t){return true;}
 void Vec2(UE::UObject* o,const wchar_t* f,double x,double y){if(std::wstring_view(f)==L"SetRenderTranslation"){o->x=x;o->y=y;}}
 UE::UObject* FindInTree(UE::UObject* o,std::string_view name){if(!o)return nullptr; if(o->name==name)return o;for(auto* c:o->children)if(auto* found=FindInTree(c,name))return found;return nullptr;}
}
bool Bounds(UE::UObject* o,inventoryheaders::Box& b){if(!o)return false;b={o->x,o->y,o->w,o->h};return true;}
UE::UObject* Slot(UE::UObject* o){return o;}
'''+column+place+r'''
int main(){unsigned checks=0,bad=0;auto check=[&](bool ok,const char* why){++checks;if(!ok){++bad;std::printf("FAIL %s\n",why);}};
 UE::UObject native,overlay,widget;widget.parent=&native;
 double tx=0,ty=0;
'''
if 'double&' in place:
    code += 'check(Place(&overlay,&widget,{200,50,44,40},tx,ty),"placement succeeds");\n'
else:
    code += 'check(Place(&overlay,&widget,{200,50,44,40},0,0),"placement succeeds");\n'
code += r'''
 check(widget.parent==&native,"controller target remains in native filter bar");
 check(ui::mutations==0,"layout never reparents registered navigation controls");
 UE::UObject row,horizontal,health,nested,price;horizontal.name="inv_entry_horizontal";health.name="inv_entry_health";nested.name=price.name="inv_entry_value";
 row.children={&horizontal};horizontal.children={&health,&price};health.children={&nested};
 check(Column(&row,"inv_entry_value")==&price,"coin header uses direct price column rather than nested health value");
 check(Column(&row,"inv_entry_health")==&health,"condition column remains discoverable");
 check(Column(&row,"absent")==nullptr,"missing column is safe");
 std::printf("%u checks, %u failures\n",checks,bad);return bad?1:0;}
'''
cpp=out/'parent.cpp';exe=out/'parent.exe';cpp.write_text(code)
subprocess.run(['cl','/nologo','/EHsc','/std:c++latest',str(cpp),f'/Fe:{exe}',f'/Fo:{out}/'],check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
