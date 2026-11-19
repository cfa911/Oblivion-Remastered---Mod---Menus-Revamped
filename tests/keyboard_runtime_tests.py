"""Execute the real label/layout functions against a small UMG boundary double.

--baseline executes the committed functions, to distinguish a behavioural regression from build failures.
"""
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
source = (subprocess.check_output(["git", "show", "HEAD:src/keyboard/Keyboard.cpp"], cwd=root).decode()
          if "--baseline" in sys.argv else (root / "src/keyboard/Keyboard.cpp").read_text())


def function(signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


harness = r'''
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
namespace UE { struct UObject {}; }
int nativeCalls=0, otherCalls=0; bool fontReady=true;
namespace ui {
UE::UObject label, slot;
UE::UObject sourceMaterial, ownMaterial;
int holeWrites=0; bool changedSource=false;
struct Call {
 Call(UE::UObject*,const wchar_t*) {}
 template<class T> void Set(const char*,T) {}
 bool Run() { return true; }
 template<class T> T Get(const char*) { return &ownMaterial; }
};
UE::UObject* Load(const wchar_t*) { return &sourceMaterial; }
void SetMidScalar(UE::UObject* m,const wchar_t* n,float value) {
 if(m==&sourceMaterial) changedSource=true;
 if(std::wstring(n)==L"Hole?"&&value==0) ++holeWrites;
}
UE::UObject* AddToCanvas(UE::UObject*,const wchar_t*,const wchar_t*,UE::UObject** s) { *s=&slot; return &label; }
void Anchors(UE::UObject*,double,double,double,double) {}
void Vec2(UE::UObject*,const wchar_t*,double,double) {}
bool CallFirst(UE::UObject*,const wchar_t*,const void*,size_t) { return true; }
}
namespace uix {
void SetText(UE::UObject*,const std::string&) {}
void Ink(UE::UObject*,const float*) {}
bool ApplyFont(UE::UObject*,double) { ++otherCalls; return true; }
bool ApplyItemFont(UE::UObject*,double) { ++nativeCalls; return fontReady; }
}
constexpr float kInk[4]={.1f,.06f,.03f,1};
const float* Ink() { return kInk; }
bool g_mirror=false; constexpr int kCols=10,kRows=4;
double Aspect() { return 16./9; }
struct Key { int row,col,span; };
const std::vector<Key>& Keys() { static const auto keys=[] { std::vector<Key> k; for(int r=0;r<4;++r) for(int c=0;c<10;++c) k.push_back({r,c,1}); return k; }(); return keys; }
namespace kbcfg { struct Values { int leftPercent=49,topPercent=14,rightPercent=93,bottomPercent=87; }; const Values& Get() { static Values v; return v; } }
struct Rect { double x0,y0,x1,y1; };
struct Layout { Rect panel{},header{},hint{}; std::vector<Rect> key; };
Rect reference{866./1920,116.833354/1080,1837./1920,1002.166667/1080};
Rect DetailsPanel() { return g_mirror ? Rect{1-reference.x1,reference.y0,1-reference.x0,reference.y1} : reference; }
UE::UObject* AddImage(UE::UObject*,const std::wstring&,const Rect&,const float*,const float*,float) { return &ui::label; }
'''
harness += function("Layout MakeLayout()") + "\n" + function("UE::UObject* AddLabel(")
dh_start = source.index("const double dhFull =")
harness += "\ndouble IconHeight(const Layout& layout, double scale) { " + source[dh_start:source.index(";", dh_start)+1] + " return dhFull; }\n"
if "--baseline" not in sys.argv:
    page_start = source.index('{   // The same masked Inventory details parchment')
    page_end = source.index('std::vector<reflect::Handle> imgs', page_start)
    harness += "\nbool BuildPage(UE::UObject* canvas, const Layout& layout, const std::wstring& n) " + source[page_start:page_end].strip()[:-1] + "return true; }\n"
else:
    harness += "\nbool BuildPage(UE::UObject*,const Layout&,const std::wstring&) { return true; }\n"
harness += r'''
int main() {
 int failures=0; auto check=[&](bool ok,const char* why) { if(!ok) { ++failures; std::printf("FAIL %s\n",why); } };
 auto close=[](double a,double b) { return std::abs(a-b)<.000001; };
 for(bool mirror:{false,true}) {
  g_mirror=mirror; const auto expected=DetailsPanel(); const auto layout=MakeLayout();
  check(close(layout.panel.x0,expected.x0)&&close(layout.panel.x1,expected.x1)&&close(layout.panel.y0,expected.y0)&&close(layout.panel.y1,expected.y1),"keyboard matches details page in both orientations");
  check(layout.key.size()==40,"all forty keys remain present");
  for(const auto& key:layout.key) check(key.x0>=layout.panel.x0&&key.x1<=layout.panel.x1&&key.y0>=layout.panel.y0&&key.y1<=layout.panel.y1,"keys stay inside the page");
  const auto& key=layout.key.front();
  const double center=(layout.panel.x0+layout.panel.x1)*.5;
  const double width=1069./2559, top=537./1450, height=596./1450;
  const double row=height*88./431;
  check(close(key.x0,center-width*.5+width/10*.07),"owner purple guide determines grid width and left edge");
  check(close(key.y0,top+row*.08)&&close(layout.key.back().y1,top+row*3.92),"owner purple guide determines grid height and vertical placement");
  check(close((key.x0+layout.key[9].x1)*.5,center),"grid remains horizontally centered");
  check(close(layout.header.y0,368./1450)&&close(layout.header.y1,487./1450),"Search moves into the yellow guide");
  check(close(layout.header.x1-layout.header.x0,321./2559)&&close((layout.header.x0+layout.header.x1)*.5,center),"Search has the yellow guide width and shares the grid center");
  check(close((layout.hint.y0+layout.hint.y1)*.5,top+height*.92),"hint row follows the enlarged purple frame");
  check(std::abs(IconHeight(layout,1)*1080-29)<.001,"icons grow proportionally with the owner-sized content");
  UE::UObject page;
  check(BuildPage(&page,layout,L"test")&&ui::holeWrites>0&&!ui::changedSource,"RMI-112 page disables the cutout on its own material");
 }
 UE::UObject canvas;
 check(AddLabel(&canvas,L"letter",reference,36,"a")!=nullptr,"label builds with a ready native font");
 check(nativeCalls==1&&otherCalls==0,"keyboard requests the item-name font instead of Robinson");
 fontReady=false;
 check(AddLabel(&canvas,L"retry",reference,36,"a")==nullptr,"unavailable native font retries instead of retaining a wrong font");
 std::printf("keyboard runtime: %d failures\n",failures); return failures?1:0;
}
'''
(out / "keyboard.cpp").write_text(harness)
(out / "run.bat").write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\n'
    f'cd /d "{out}"\ncl /nologo /std:c++20 /EHsc keyboard.cpp /Fe:keyboard.exe > compile.log 2>&1\n'
    'if errorlevel 1 (type compile.log & exit /b 2)\nkeyboard.exe\nexit /b %errorlevel%\n')
result = subprocess.run(["cmd", "/c", str(out / "run.bat")])
sys.exit(result.returncode)
