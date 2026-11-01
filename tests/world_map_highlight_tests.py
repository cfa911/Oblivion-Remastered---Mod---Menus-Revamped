"""Actual ring renderer: displayed-image pan invalidates without model/input changes."""
from pathlib import Path
import subprocess,sys
import source_paths
root=Path(__file__).resolve().parents[1];out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
body=source_paths.function((root/'src/search/WorldMapSearch.cpp').read_text(),'void Highlight(')
prefix=r'''
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <cmath>
#include <iostream>
namespace UE { struct UObject {}; }
UE::UObject object;
namespace reflect { struct Handle {UE::UObject* p=&object;}; UE::UObject* Get(const Handle& h){return h.p;} }
namespace menus { enum class Menu {kWorldMap}; }
namespace search::detail { struct Bar {reflect::Handle frame;double w=400;};std::array<Bar,1> g_bar; }
bool g_highlight=true,g_ringReady=true,g_displayReady=false,shown=false,valid=true;
int g_displaySettle=0;
reflect::Handle g_ring,g_canvas,g_ringSlot;
ULONGLONG g_nextHighlight=0;
double g_highlightX=10,g_highlightY=20,g_displayX=0,g_displayY=0;
float g_highlightZoom=1;
double displayedX=0.4,displayedY=0.5,modelX=10;float modelZoom=1;
namespace worldmap {
bool Position(double& x,double& y,float& z){x=modelX;y=20;z=modelZoom;return true;}
bool DisplayedCenter(double& x,double& y){x=displayedX;y=displayedY;return valid;}
UE::UObject* View(){return &object;}
}
namespace ui {
void Visible(UE::UObject*,bool value){shown=value;}
bool Measure(UE::UObject*,double& x,double& y,double& w,double& h){x=y=0;w=400;h=300;return true;}
void Vec2(UE::UObject*,const wchar_t*,double,double){}
}
'''
main=r'''
int main(){int checks=0,failed=0;auto check=[&](bool ok,const char* why){++checks;if(!ok){++failed;std::cerr<<"FAIL "<<why<<"\n";}};
auto tick=[](){g_nextHighlight=0;Highlight();};
tick();tick();tick();check(g_highlight&&shown,"stationary selected map displays ring after layout settles");
displayedX+=0.00000003;tick();check(g_highlight,"captured cursor-only rounding noise preserves ring");
displayedX+=0.18;tick();check(!g_highlight&&!shown,"actual image pan dismisses ring with unchanged model position");
g_highlight=true;g_displayReady=false;g_displaySettle=0;tick();tick();tick();check(g_highlight&&shown,"new selection re-arms ring at current map center");
modelZoom=2;tick();check(!g_highlight&&!shown,"actual zoom change still dismisses ring");
modelZoom=1;g_highlight=true;g_displayReady=false;g_displaySettle=0;tick();tick();tick();modelX=11;tick();check(!g_highlight,"native model recenter still dismisses ring");
std::cout<<checks<<" checks, "<<failed<<" failures\n";return failed?1:0;}
'''
cpp=out/'highlight.cpp';exe=out/'highlight.exe';cpp.write_text(prefix+body+main)
subprocess.run(['cl','/nologo','/EHsc','/std:c++20',str(cpp),'/Fo'+str(out/'highlight.obj'),'/Fe'+str(exe)],check=True)
subprocess.run([str(exe)],check=True)
