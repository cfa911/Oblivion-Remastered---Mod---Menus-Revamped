"""Replay the recorded cursor-only and native-pan geometries through the real adapter."""
from pathlib import Path
import subprocess,sys
import source_paths
root=Path(__file__).resolve().parents[1];out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
body=source_paths.function((root/'src/engine/WorldMap.cpp').read_text(),'bool DisplayedCenter(')
prefix=r'''
#include <cmath>
#include <cstring>
#include <iostream>
namespace UE { struct UObject { alignas(8) unsigned char bytes[16]{}; void* GetClass(){return this;} }; }
UE::UObject page,map,image,view;bool opened=true,measured=true;
UE::UObject* Widget(){return opened?&page:nullptr;} UE::UObject* View(){return &view;}
namespace reflect { int Offset(void*,const char*){return 0;} template<class T>T* At(UE::UObject* o,int){return reinterpret_cast<T*>(o->bytes);} }
double imageX=-1054.6402587890625,imageY=-646.3701171875,imageW=4295.9996337890625,imageH=3545;
double viewX=0,viewY=0;
namespace ui {
int SizeOf(void*,const char*){return sizeof(void*);}
bool Measure(UE::UObject* obj,double& x,double& y,double& w,double& h){
 if(!measured)return false;
 if(obj==&image){x=imageX;y=imageY;w=imageW;h=imageH;}
 else {x=viewX;y=viewY;w=1919.9998779296875;h=1079.9998779296875;}return true;
}
}
'''
main=r'''
int main(){int checks=0,failed=0;auto check=[&](bool ok,const char* why){++checks;if(!ok){++failed;std::cerr<<"FAIL "<<why<<"\n";}};
*reinterpret_cast<UE::UObject**>(page.bytes)=&map;*reinterpret_cast<UE::UObject**>(map.bytes)=&image;
double x=0,y=0;check(DisplayedCenter(x,y),"captured selected-map geometry resolves");const double baselineX=x,baselineY=y;
imageX=-1054.640380859375;imageW=4295.99951171875;
check(DisplayedCenter(x,y)&&std::abs(x-baselineX)<0.000001&&std::abs(y-baselineY)<0.000001,"captured cursor-only noise remains below tolerance");
imageX=-1824.89013671875;imageY=-116.1202392578125;imageW=4295.999755859375;imageH=3544.9998779296875;
check(DisplayedCenter(x,y)&&std::abs(x-baselineX)>0.1&&std::abs(y-baselineY)>0.1,"captured native pan changes displayed map center");
const double px=x,py=y;viewX+=100;viewY+=200;imageX+=100;imageY+=200;
check(DisplayedCenter(x,y)&&std::abs(x-px)<0.000001&&std::abs(y-py)<0.000001,"moving whole viewport does not simulate map pan");
imageW=0;check(!DisplayedCenter(x,y),"invalid image geometry rejected before division");imageW=4296;
opened=false;check(!DisplayedCenter(x,y),"closed map cannot resolve displayed center");
std::cout<<checks<<" checks, "<<failed<<" failures\n";return failed?1:0;}
'''
cpp=out/'geometry.cpp';exe=out/'geometry.exe';cpp.write_text(prefix+body+main)
subprocess.run(['cl','/nologo','/EHsc','/std:c++20',str(cpp),'/Fo'+str(out/'geometry.obj'),'/Fe'+str(exe)],check=True)
subprocess.run([str(exe)],check=True)
