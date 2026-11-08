"""Execute production mouse input, tick publication/repaint, and popup painting."""
from pathlib import Path
import subprocess
import sys
import source_paths

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
input_source = (root / "src/sorting/PickerInput.cpp").read_text()
message = source_paths.function(input_source, "bool Picker::Message(")
controller = source_paths.function(input_source, "void Controller(")
owner = source_paths.function(controller, "if (owner)")
pad_prefix = owner[owner.index("auto& picker = *owner;"):owner.index("if (picker.open)")]
pad_popup = source_paths.function(owner, "if (picker.open)")
paint = source_paths.function((root / "src/sorting/PickerPopup.cpp").read_text(), "void Picker::PaintController(")
tick = source_paths.function((root / "src/sorting/PickerTick.cpp").read_text(), "void Picker::Tick(")
# Execute the real post-transition tail; only UE geometry/reflection uses fixtures.
tail = tick[tick.index("// A nested view changed after"):tick.rfind("}")]
rect = source_paths.function((root / "src/sorting/PickerInternal.h").read_text(), "struct Rect")
prefix = r'''
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include "core/UiTheme.h"
#include "sorting/rules/ItemSort.h"
#include "sorting/rules/PickerHighlight.h"
#include "sorting/rules/ControllerPolicy.h"
#include "sorting/rules/StolenFilter.h"
#include "sorting/rules/EnchantedFilter.h"
'''
prefix += rect + r''';
struct Widget { Rect rect; bool highlighted=false; std::uint8_t visibility=0; };
namespace reflect { using Handle=Widget*; Widget* Get(Handle p){return p;} }
namespace mrconfig { struct Values { uistyle::Preset stylePreset=uistyle::Preset::Classic; };
const Values& Get(){static Values v;return v;} }
namespace ui {
void RoundedBox(Widget* p,bool,const float* fill,const float*,float,float){if(p)p->highlighted=fill[3]>0;}
void CallFirst(Widget* p,const wchar_t*,const void* value,unsigned){if(p)p->visibility=*static_cast<const std::uint8_t*>(value);}
bool Measure(Widget* p,double& x,double& y,double& w,double& h){if(!p)return false;x=p->rect.x;y=p->rect.y;w=p->rect.w;h=p->rect.h;return true;}
}
constexpr float kClear[4]={};
pickerpad::State g_pad;
struct Picker {
 std::mutex inputLock;
 bool inputActive=true,inputOpen=false,swallowUp=false,swallowRightUp=false,swallowEscape=false,groupInput=false;
 bool commandFromController=false,open=true,paintedOpen=false,controllerPopup=false,controllerFocus=false,styled=true;
 bool mousePositionKnown=false; double mouseX=0,mouseY=0;
 std::uint64_t doubleClickUntil=0,nextHitMeasure=0;
 int command=-1,kind=0,paintedMouseHoverCell=-1;
 std::atomic<int> mouseHoverCell{-1}; std::atomic<bool> active{true};
 std::atomic<bool> mouseInputActive{false}; bool paintedMouseInputActive=false;
 unsigned cursor=0; int paints=0;
 static constexpr int kSorts=static_cast<int>(itemsort::Key::kCount);
 Rect cellsPx[itemtags::kMaxChoices],sortPx[kSorts],headerPx,popupPx,stolenPx,enchantedPx;
 itemtags::Tab view=itemtags::Tab::Potions;
 Widget paper,widgets[itemtags::kMaxChoices]; reflect::Handle popup=&paper,headerFrame=nullptr,cells[itemtags::kMaxChoices];
 Picker(){for(unsigned i=0;i<itemtags::kMaxChoices;++i)cells[i]=&widgets[i];}
 void PaintGroup(){++paints;} void PaintStolen(){} void PaintEnchanted(){} void StyleCells(){}
 void BrushHeader(const wchar_t*){} const wchar_t* HeaderIcon(){return L"";} bool GroupSorts(){return false;}
 bool Message(UINT,WPARAM,LPARAM); void PaintController(); void Frame(int cmd,std::uint64_t now,double a_viewportScale=1);
 void Queue(int cmd){command=cmd;commandFromController=true;}
 void Layout(itemtags::Tab tab,double shift=0){view=tab;unsigned n=itemtags::Info(view).count;
  paper.rect={shift,0,double(pickergrid::Width(n)),double(pickergrid::Height(n))};
  for(unsigned i=0;i<itemtags::kMaxChoices;++i)widgets[i].rect=i<n?Rect{shift+pickergrid::CellX(i,n),double(pickergrid::CellY(i,n)),pickergrid::CellWidth,pickergrid::CellHeight}:Rect{};
  nextHitMeasure=0;
 }
 void Move(int x,int y){Message(WM_MOUSEMOVE,0,MAKELPARAM(x,y));}
};
'''
main = r'''
unsigned checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
int main(){
 Picker p;p.Layout(itemtags::Tab::Potions);p.Frame(-1,100);
 p.Move(20,110);p.Frame(-1,101);
 Check(p.mouseHoverCell==3&&p.widgets[3].highlighted,"mouse movement paints the hit cell");
 Check(!p.controllerPopup&&p.cursor==0,"mouse hover does not acquire controller focus or change its cursor");
 Pad(p);p.Frame(-1,102);
 Check(p.widgets[3].highlighted&&!p.widgets[0].highlighted,"idle connected-pad poll cannot add All focus during mouse use");
 p.Layout(itemtags::Tab::PoisonEffects);p.Frame(3,200);
 Check(p.mouseHoverCell==2&&p.widgets[2].highlighted&&!p.widgets[3].highlighted,"stationary pointer follows three-column to two-column reflow");
 p.Layout(itemtags::Tab::Potions);p.Frame(-3,300);
 Check(p.mouseHoverCell==3&&p.widgets[3].highlighted&&!p.widgets[2].highlighted,"back-navigation restores hover from measured geometry");
 p.Layout(itemtags::Tab::Potions,500);p.Frame(-1,400);
 Check(p.mouseHoverCell==-1&&!p.widgets[3].highlighted,"moving popup away clears hover without a mouse message");
 p.Layout(itemtags::Tab::Potions);p.Frame(-1,500);
 p.cursor=1;Pad(p,pickerpad::Right);p.Frame(-1,500);
 Check(p.widgets[p.cursor].highlighted&&!p.widgets[3].highlighted&&!p.mouseInputActive,"real controller navigation shows only controller focus");
 unsigned padCursor=p.cursor;
 p.Move(900,900);p.Frame(-1,501);
 Check(p.mouseHoverCell==-1&&!p.widgets[padCursor].highlighted&&!p.widgets[3].highlighted&&p.cursor==padCursor,"mouse movement outside cells clears fill without changing controller cursor");
 p.Move(20,110);p.Frame(-1,502);p.open=false;p.Frame(-2,503);
 Check(p.mouseHoverCell==-1&&!p.controllerPopup&&!p.widgets[1].highlighted&&!p.widgets[3].highlighted,"closing clears mouse and controller popup highlights");
 p.open=true;p.Frame(-4,600);
 Check(p.mouseHoverCell==3&&p.widgets[3].highlighted,"reopening recomputes hover for a stationary pointer");
 int previous=p.paints;p.Move(21,111);p.Frame(-1,601);
 Check(p.paints==previous,"movement within the same cell avoids redundant repaint");
 Pad(p);Pad(p,pickerpad::Down);p.Frame(-1,602);
 unsigned controllerCursor=p.cursor;
 p.Move(21,111);p.Frame(-1,603);
 Check(!p.mouseInputActive&&p.widgets[controllerCursor].highlighted,"identical synthetic mouse coordinates do not steal controller focus");
 p.Message(WM_RBUTTONDOWN,0,MAKELPARAM(900,900));p.Frame(-1,604);
 Check(p.mouseInputActive,"a real mouse click reclaims mouse mode even without movement");
 p.inputActive=false;p.Move(900,900);Check(p.mouseHoverCell==3,"inactive input does not alter hover");
 std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
'''
cpp = out / "picker_mouse_hover_tests.cpp"
exe = out / "picker_mouse_hover_tests.exe"
pad = '\nvoid Pad(Picker& target,std::uint16_t buttons=0){static std::uint64_t time=1000; pickerpad::Input raw{buttons,0,0};g_pad.Sample(raw,++time);auto* owner=&target;\n' + pad_prefix + pad_popup + '}\n'
cpp.write_text(prefix + message + paint + "\nvoid Picker::Frame(int cmd,std::uint64_t now,double a_viewportScale){\n" + tail + "}\n" + pad + main)
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", *source_paths.include_flags(), str(cpp), f"/Fe:{exe}", f"/Fo:{out}/", "user32.lib"], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
