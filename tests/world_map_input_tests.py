"""Run actual map-result window/controller input handlers against input state doubles."""
from pathlib import Path
import subprocess
import sys
import source_paths
root=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]).resolve(); out.mkdir(parents=True,exist_ok=True)
src=(root/'src/search/WorldMapSearch.cpp').read_text()
functions='\n'.join(source_paths.function(src,name) for name in ('bool Message(', 'bool Controller('))
prefix=r'''
#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>
#include <atomic>
#include <array>
#include <mutex>
#include <cstdint>
#include <iostream>
namespace search::detail { std::atomic<bool> g_typing{false}, g_external{false}; }
namespace mrconfig { struct Values { int stickDeadzone=16000; }; inline const Values& Get(){ static Values v; return v; } }
constexpr int kRows=12;
struct Rect { double x=0,y=0,w=0,h=0; bool valid=false; };
std::array<Rect,kRows> g_hits;
std::array<int,kRows> g_hitIndex;
std::mutex g_hitsMutex;
std::atomic<bool> g_highlight{false};
std::atomic<bool> g_active{true},g_visible{true},g_dismiss{false},g_mouseUp{false};
std::atomic<int> g_move{0},g_choose{-1},g_hover{-1};
std::atomic<unsigned> g_swallowKeys{0};
std::atomic<std::uint64_t> g_renderedVersion{11},g_commandVersion{0};
'''
main=r'''
int main() {
 int checks=0,failed=0;
 auto check=[&](bool b,const char* label){++checks;if(!b){++failed;std::cerr<<"FAIL "<<label<<'\n';}};
 check(Message(nullptr,WM_KEYDOWN,VK_RETURN,0) && g_choose==100000,"Enter selects current result");
 check(g_commandVersion==11,"selection belongs to the rendered result revision");
 g_visible=false;
 check(Message(nullptr,WM_KEYUP,VK_RETURN,0),"Enter release swallowed after selection");
 g_visible=true;g_choose=-1;
 check(Message(nullptr,WM_KEYDOWN,VK_DOWN,0) && g_move==1,"Down moves result selection");
 check(Message(nullptr,WM_KEYDOWN,VK_ESCAPE,0) && g_dismiss,"Escape dismisses results");
 g_visible=false;
 check(Message(nullptr,WM_KEYUP,VK_ESCAPE,0),"Escape release cannot close map");
 g_visible=true;g_dismiss=false;
 g_hits[0]={0,0,100,30,true};g_hitIndex[0]=2;
 check(Message(nullptr,WM_LBUTTONDOWN,0,MAKELPARAM(10,10)) && g_choose==2,"mouse selects hit row");
 g_visible=false;
 check(Message(nullptr,WM_LBUTTONUP,0,MAKELPARAM(10,10)),"mouse release swallowed after focus");
 g_visible=true;
 check(!Message(nullptr,WM_LBUTTONDOWN,0,MAKELPARAM(150,150)),"outside clicks remain native");
 g_hits[kRows-1]={0,100,100,30,true};g_hitIndex[kRows-1]=11;
 check(Message(nullptr,WM_LBUTTONDOWN,0,MAKELPARAM(10,110)) && g_choose==11,"twelfth visible row remains selectable");
 Message(nullptr,WM_LBUTTONUP,0,MAKELPARAM(10,110));
 search::detail::g_external=true;g_choose=-1;
 check(!Message(nullptr,WM_KEYDOWN,VK_RETURN,0) && g_choose==-1,"controller keyboard owns input exclusively");
 search::detail::g_external=false;g_swallowKeys=0;
 WORD buttons=XINPUT_GAMEPAD_A;SHORT lx=12345,ly=-12345,rx=3000,ry=3000;
 check(Controller(buttons,lx,ly,rx,ry,false) && !(buttons&XINPUT_GAMEPAD_A) && !lx && !ly && !rx && !ry,"result confirm cannot activate native travel or pan");
 check(g_choose==100000,"controller confirms selection");
 g_visible=false;buttons=XINPUT_GAMEPAD_A;
 check(!Controller(buttons,lx,ly,rx,ry,false) && !buttons,"held confirm swallowed after results close");
 buttons=0;Controller(buttons,lx,ly,rx,ry,false);
 g_visible=true;buttons=XINPUT_GAMEPAD_B;g_dismiss=false;
 check(Controller(buttons,lx,ly,rx,ry,false) && !buttons && g_dismiss,"controller Back dismisses without map Back");
 buttons=0;Controller(buttons,lx,ly,rx,ry,false);
 g_choose=-1;buttons=XINPUT_GAMEPAD_A;
 check(!Controller(buttons,lx,ly,rx,ry,true) && buttons==XINPUT_GAMEPAD_A && g_choose==-1,"open keyboard owns controller");
 g_visible=false;search::detail::g_typing=false;g_highlight=true;
check(!Message(nullptr,WM_MOUSEMOVE,0,MAKELPARAM(20,20)) && g_highlight,"plain mouse hover preserves focus ring");
check(!Message(nullptr,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(30,20)) && g_highlight,"drag input alone cannot dismiss ring before actual position changes");
g_highlight=true;
check(!Message(nullptr,WM_KEYDOWN,VK_LEFT,0) && g_highlight,"keyboard input alone cannot dismiss ring before actual position changes");
g_highlight=true;buttons=0;lx=20000;ly=rx=ry=0;
check(!Controller(buttons,lx,ly,rx,ry,false) && g_highlight && lx==20000,"controller input alone cannot dismiss ring before actual position changes");
g_highlight=true;lx=100;
check(!Controller(buttons,lx,ly,rx,ry,false) && g_highlight,"controller deadzone jitter preserves ring");
g_visible=true;g_highlight=true;lx=20000;
check(Controller(buttons,lx,ly,rx,ry,false) && g_highlight,"result navigation does not dismiss focus ring");
g_visible=false;g_active=false;g_highlight=true;
check(!Message(nullptr,WM_MOUSEMOVE,MK_LBUTTON,0) && g_highlight,"other menus cannot clear map highlight");
std::cout<<checks<<" checks, "<<failed<<" failures\n";
 return failed?1:0;
}
'''
cpp=out/'map_input.cpp';exe=out/'map_input.exe';cpp.write_text(prefix+functions+main)
subprocess.run(['cl','/nologo','/std:c++20','/EHsc',str(cpp),'/Fo'+str(out/'input.obj'),'/Fe'+str(exe),'user32.lib'],check=True)
subprocess.run([str(exe)],check=True)
