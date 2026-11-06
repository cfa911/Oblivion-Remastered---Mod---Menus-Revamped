"""Exercise the real Alchemy controller: text owners must keep X/Y/A/B."""
from pathlib import Path
import subprocess
import sys
import source_paths

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
prefix = r'''
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include "sorting/rules/AlchemyRules.h"
#include "sorting/rules/ControllerPolicy.h"
using namespace alchemyrules;
ULONGLONG testNow=0;
#define GetTickCount64() testNow
pickerpad::State g_sortNav;
std::atomic<int> g_sortDirection{-1};
SortState g_sortState;
bool g_dirtySort=false;
int g_nextLayout=0;
struct Rect { double x=0,y=0,w=0,h=0; bool ok=false; };
std::mutex g_rectLock;
std::vector<Rect> g_chipRects;
constexpr std::uint16_t kPadUp=1,kPadDown=2,kPadLeft=4,kPadRight=8,kPadA=0x1000,kPadB=0x2000,kPadX=0x4000,kPadY=0x8000;
constexpr ULONGLONG kHoldClearMs=500;
constexpr int kIcons=5;
constexpr std::uint16_t kPadName=0x40;
constexpr std::uint16_t kPadLB=0x100,kPadRB=0x200;
std::atomic<bool> g_nameKeyboardRequest{false}, g_canName{true};
namespace search { bool typing=false; bool Typing() { return typing; } }
namespace keyboard { bool taking=false; bool Taking() { return taking; } }
std::atomic<bool> g_openFlag{true},g_filtersVisible{false},g_nameTyping{false},g_headerNow{false},g_hoverNow{false},g_openFiltersRequest{false},g_clearAllRequest{false};
std::atomic<int> g_padFocus{-1},g_chipFocus{-1},g_chipCount{3},g_activeIcon{0},g_iconClick{-1},g_chipClick{-1};
std::uint16_t g_padSwallow=0,g_lastButtons=0;
HoldTracker g_xHold,g_yHold;
bool Enabled() { return true; }
'''
main = r'''
int main() {
int checks=0,failed=0;
auto check=[&](bool ok,const char* why) { ++checks; if(!ok) { ++failed; std::cerr<<"FAIL "<<why<<"\n"; } };
auto reset=[] { g_padSwallow=g_lastButtons=0; g_padFocus=g_chipFocus=-1; g_openFiltersRequest=false; g_xHold.Reset(); g_yHold.Reset(); };
auto tap=[](std::uint16_t key) { auto b=key; Controller(b); b=0; Controller(b); };
reset(); search::typing=true; tap(kPadY);
check(g_padFocus==-1,"search typing cannot enter sort mode");
reset(); search::typing=false; keyboard::taking=true; tap(kPadX);
check(!g_openFiltersRequest,"controller keyboard X cannot open Filters");
reset(); tap(kPadY);
check(g_padFocus==-1,"controller keyboard Y cannot enter sort mode");
reset(); keyboard::taking=false; g_nameTyping=true; tap(kPadY);
check(g_padFocus==-1,"potion-name typing cannot enter sort mode");
reset(); g_nameTyping=false;
auto native=static_cast<std::uint16_t>(kPadX); Controller(native);
check(native==kPadX && !g_openFiltersRequest,"X Create passes through immediately without opening Filters");
Sleep(510); native=kPadX; Controller(native);
check(native==kPadX && !g_clearAllRequest,"held X remains native and cannot clear filters");
reset(); native=kPadY; Controller(native);
check(native==kPadY,"Y Filters passes through immediately");
Sleep(510); native=kPadY; Controller(native);
check(native==kPadY && !g_clearAllRequest,"held Y passes to native Clear all without a duplicate request");
native=0; Controller(native);
check(g_padFocus==-1 && g_chipFocus==-1 && !g_openFiltersRequest,"Y release does not enter a custom mode or duplicate Filters");
reset(); native=kPadRB; Controller(native);
check(g_padFocus==g_activeIcon && native==0,"RB enters sort and is consumed");
native=0; Controller(native); tap(kPadRight); tap(kPadA);
check(g_iconClick==4,"sort follows visual A-Z to Colors and confirm still works");
tap(kPadB); check(g_padFocus==-1,"B leaves sorting");
reset(); native=kPadLB; Controller(native);
check(g_chipFocus==0 && native==0,"LB enters Selected Filters and is consumed");
native=0; Controller(native); tap(kPadRight); tap(kPadA);
check(g_chipClick==1,"Selected Filters navigation and confirm still work");
tap(kPadB); check(g_chipFocus==-1,"B leaves Selected Filters");
reset(); g_filtersVisible=true; native=kPadLB|kPadRB; Controller(native);
check(native==(kPadLB|kPadRB) && g_padFocus==-1 && g_chipFocus==-1,"Filters panel owns shoulders for its own controls and future search");
reset(); g_filtersVisible=false; keyboard::taking=true; native=kPadLB|kPadRB; Controller(native);
check(native==(kPadLB|kPadRB) && g_padFocus==-1 && g_chipFocus==-1,"keyboard owns shoulders without entering Alchemy modes");
reset(); keyboard::taking=false;
reset(); tap(kPadName);
check(g_nameKeyboardRequest,"L3 requests the separate potion-name keyboard");
reset(); g_nameKeyboardRequest=false; g_nameTyping=true; tap(kPadName);
check(g_nameKeyboardRequest,"L3 can open the controller keyboard when native name editing already has focus");
reset(); g_nameTyping=false; g_sortNav={}; g_sortState={SortKey::Name,true}; g_iconClick=-1;
tap(kPadRB);
short x=25000,y=0; std::uint16_t buttons=0; Drive(buttons,x,y);
check(g_padFocus==4 && x==0 && y==0,"sort stick Right follows visual order and never leaks to native list");
x=25000; Drive(buttons,x,y);
check(g_padFocus==4,"held stick does not move again on every input read");
x=0; Drive(buttons,x,y); x=-25000; Drive(buttons,x,y);
check(g_padFocus==0 && x==0,"sort stick Left moves back");
x=0; Drive(buttons,x,y); tap(kPadUp); ApplySortInput();
check(g_sortState.key==SortKey::Name && !g_sortState.descending,"D-pad Up explicitly sorts focused column ascending");
tap(kPadDown); ApplySortInput();
check(g_sortState.descending,"D-pad Down explicitly sorts focused column descending");
tap(kPadDown); ApplySortInput();
check(g_sortState.descending,"repeated Down stays descending instead of toggling");
y=25000; Drive(buttons,x,y); ApplySortInput();
check(!g_sortState.descending && y==0,"sort stick Up sets ascending and is consumed");
y=0; Drive(buttons,x,y); y=-25000; Drive(buttons,x,y); ApplySortInput();
check(g_sortState.descending && y==0,"sort stick Down sets descending and is consumed");
y=0; Drive(buttons,x,y); tap(kPadRight); tap(kPadUp); ApplySortInput();
check(g_sortState.key==SortKey::Colors && !g_sortState.descending,"direction applies to focused column even if it was not the active sort");
tap(kPadB); g_sortDirection=-1; buttons=kPadUp; x=0;y=25000;Drive(buttons,x,y);
check(buttons==kPadUp && y==25000 && g_sortDirection==-1,"outside Sort mode Up and analog stay native");
buttons=0;x=y=0;Drive(buttons,x,y); tap(kPadLB); buttons=kPadDown;y=-25000;Drive(buttons,x,y);
check(g_sortDirection==-1 && y==0,"Selected Filters owns analog but never changes sort direction");
reset(); g_sortNav={}; g_activeIcon=1; tap(kPadRB);
const int clockwise[]={0,4,2,3,1};
for(int want:clockwise) { tap(kPadRight); check(g_padFocus==want,"carousel wraps Count A-Z Colors Weight Price in visual order"); }
const int counterclockwise[]={3,2,4,0,1};
for(int want:counterclockwise) { tap(kPadLeft); check(g_padFocus==want,"carousel reverse follows visual order"); }
reset(); g_sortNav={};g_chipCount=4;
g_chipRects={{0,0,70,20,true},{80,0,100,20,true},{0,30,90,20,true},{100,30,90,20,true}};
tap(kPadLB); tap(kPadDown);
check(g_chipFocus==2,"Selected Filters D-pad Down moves to nearest chip on next row");
tap(kPadRight);tap(kPadUp);
check(g_chipFocus==1,"Selected Filters D-pad Up moves to nearest chip on previous row");
buttons=0;x=0;y=-25000;Drive(buttons,x,y);
check(g_chipFocus==3 && y==0,"Selected Filters analog Down crosses rows and is consumed");
y=0;Drive(buttons,x,y);x=-25000;Drive(buttons,x,y);
check(g_chipFocus==2 && x==0,"Selected Filters analog Left navigates buttons");
std::cout<<checks<<" checks, "<<failed<<" failures\n";
return failed ? 1 : 0;
}
'''
src = source_paths.read('alchemy')
cpp = out / 'alchemy_search_input_tests.cpp'
controller=source_paths.function(src, 'void Controller(')
if 'short&' in controller or 'std::int16_t&' in controller:
    adapter='void Drive(std::uint16_t& b,short& x,short& y) { Controller(b,x,y); }\nvoid Controller(std::uint16_t& b) { short x=0,y=0; Controller(b,x,y); }\n'
else:
    adapter='void Drive(std::uint16_t& b,short&,short&) { Controller(b); }\n'
apply=source_paths.function(src, 'void ApplySortInput(')
if not apply:
    start=src.index('if (const int icon = g_iconClick.exchange(-1)')
    end=src.index('g_activeIcon =',start)
    apply='void ApplySortInput() {'+src[start:end]+'}\n'
chip=source_paths.function(src, 'int MoveChipFocus(')
cpp.write_text(prefix + chip + controller + adapter + apply + main, encoding='utf-8')
exe = out / 'alchemy_search_input_tests.exe'
subprocess.run(['cl','/nologo','/EHsc','/std:c++latest',f'/I{root / "src"}',str(cpp),f'/Fe:{exe}',f'/Fo:{out}/'],check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
