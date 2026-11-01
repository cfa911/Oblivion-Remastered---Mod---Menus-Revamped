"""Exercise the actual native filter reader and brush-copy adapter with live-state doubles."""
from pathlib import Path
import subprocess
import sys
import source_paths
root=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
src=(root/'src/engine/WorldMap.cpp').read_text()
read=source_paths.function(src,'std::uint32_t ReadFilters(')
copy=source_paths.function(src,'bool CopyFilterIcon(')
prefix=r'''
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
namespace UE { struct UObject { unsigned char bytes[32]{}; void* GetClass(){return this;} }; }
namespace reflect {
struct Handle { UE::UObject* ptr=nullptr; bool live=true; };
UE::UObject* Get(const Handle& h){return h.live?h.ptr:nullptr;}
template<class T> T* At(UE::UObject* o,int offset){return offset>=0?reinterpret_cast<T*>(o->bytes+offset):nullptr;}
int Offset(void*,const char*){return 8;}
}
struct NativeFilter { reflect::Handle button,image; int activeOffset=0; };
std::array<NativeFilter,13> g_filters;
bool opened=true;int finds=0,calls=0;bool sizeOK=true;UE::UObject target;unsigned char copied[16]{};
bool Open(){return opened;} void FindFilters(){++finds;}
namespace ui {
int SizeOf(void* cls,const char*){return cls==&target&&!sizeOK?8:16;}
bool CallFirst(UE::UObject*,const wchar_t*,const void* data,std::size_t size){++calls;std::memcpy(copied,data,size);return true;}
}
'''
main=r'''
int main(){
int checks=0,failed=0;auto check=[&](bool ok,const char* why){++checks;if(!ok){++failed;std::cerr<<"FAIL "<<why<<"\n";}};
UE::UObject camp,cave,brush;camp.bytes[0]=1;cave.bytes[0]=0;
g_filters[1].button={&camp};g_filters[2].button={&cave};g_filters[1].image={&brush};
check(ReadFilters()==(1u<<1),"disabled individual type excluded independently of All");
cave.bytes[0]=1;check(ReadFilters()==((1u<<1)|(1u<<2)),"live toggle updates mask");
g_filters[1].button.live=false;check(ReadFilters()==(1u<<2),"stale control rejected");
opened=false;check(ReadFilters()==0,"closed map has no eligible filter types");opened=true;
for(int i=8;i<24;++i)brush.bytes[i]=static_cast<unsigned char>(i);
check(CopyFilterIcon(1,&target)&&calls==1&&!std::memcmp(copied,brush.bytes+8,16),"native full brush copied exactly");
check(brush.bytes[8]==8&&brush.bytes[23]==23,"native brush remains unchanged");
calls=0;sizeOK=false;check(!CopyFilterIcon(1,&target)&&!calls,"mismatched brush layout rejected");sizeOK=true;
g_filters[1].image.live=false;check(!CopyFilterIcon(1,&target)&&!calls,"stale native image rejected");
check(!CopyFilterIcon(0,&target)&&!CopyFilterIcon(13,&target)&&!CopyFilterIcon(1,nullptr),"invalid type and target rejected");
std::cout<<checks<<" checks, "<<failed<<" failures\n";return failed?1:0;
}
'''
cpp=out/'native_filters.cpp';exe=out/'native_filters.exe';cpp.write_text(prefix+read+copy+main)
subprocess.run(['cl','/nologo','/EHsc','/std:c++20','/utf-8',str(cpp),'/Fo'+str(out/'native.obj'),'/Fe'+str(exe)],check=True)
subprocess.run([str(exe)],check=True)
