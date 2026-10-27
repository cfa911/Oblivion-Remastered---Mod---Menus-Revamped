"""Run actual throttle/discovery bodies with native-widget doubles."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
source_path = root / "revamped/HoverFix.cpp"
if not source_path.exists():
    source_path = Path(sys.argv[2]) / "smoothmenus/HoverFix.cpp"
source = source_path.read_text(encoding="utf-8")

def extract(marker):
    start = source.index(marker)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

throttle = source[source.index("enum Target"):source.index("// ---- metrics")]
prefix = r"""
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>
using namespace std::chrono_literals;
using ULONGLONG = unsigned long long;
using Clock = std::chrono::steady_clock;
ULONGLONG clockMs=1000;
ULONGLONG GetTickCount64() { return clockMs; }
namespace UE {
struct UObject; struct UFunction;
using Callback=void (*)(UObject*,UFunction*,void*);
Callback dispatch=nullptr;
struct UClass;
struct Name { std::string name; std::string ToString() {return name;} };
struct UObject {
    bool live=true; int serial=1; UClass* cls=nullptr;
    UClass* GetClass() {return cls;}
    void ProcessEvent(UFunction* f,void* p) {dispatch(this,f,p);}
};
struct UClass:UObject { Name name; Name GetFName() {return name;} UObject* GetDefaultObject(bool) {return this;} };
struct UStruct:UObject {int propertiesSize=sizeof(void*);};
struct UFunction:UStruct { std::string name; };
int liveObjects=200000;
struct FUObjectArray {
 struct Free {int Num(){return 0;}} objAvailableList;
 static FUObjectArray* GetSingleton() {static FUObjectArray a;return &a;}
 int GetObjectArrayNum(){return liveObjects;}
};
template<class T> T* StaticFindObject(void*,void*,const wchar_t*) {static T obj;return &obj;}
}
namespace reflect {
struct Handle {UE::UObject* ptr=nullptr; int index=-1;};
Handle Hold(UE::UObject* o) { return {o,o?o->serial:-1};}
UE::UObject* Get(const Handle& h) {
 return h.ptr && h.ptr->live && h.ptr->serial==h.index ? h.ptr : nullptr;
}
bool IsLive(UE::UObject* o) {return o && o->live;}
}
struct Settings {
 bool inventoryHover=true,magicHover=true,containerHover=true,metrics=false;
 int hoverIntervalMs=200;
 bool objectGuard=true;int objectGuardLimit=1500000,objectGuardCooldown=10;
};
Settings settings;
namespace cfg {const Settings& Get() {return settings;}}
namespace mrconfig {
struct Values {Settings smooth;};
Values values;
Values& Get() {values.smooth=settings;return values;}
}
namespace logger {
template<class... T> void info(T&&...) {}
template<class... T> void warn(T&&...) {}
 template<class... T> void error(T&&...) {}
}
int watches=0;
namespace pe {
std::string FunctionName(UE::UFunction* f) {return f->name;}
std::string Utf8(const std::string& s) {return s;}
template<class F> bool Watch(UE::UClass*,F) {return true;}
template<class F> bool WatchFilter(UE::UClass*,F) {++watches;return true;}
template<class F> bool Intercept(UE::UClass*,F) {++watches;return true;}
}
UE::UClass mainClass, magicClass;
UE::UObject inv, magic, yours, theirs, itemA, itemB, itemC;
bool containerOpen=false;
namespace menus {
enum class Menu {kNone,kInventory,kMagic,kContainerYours,kContainerTheirs};
Menu active=Menu::kNone;
UE::UObject* Widget(Menu m) {return m==Menu::kInventory?&inv:m==Menu::kMagic?&magic:nullptr;}
UE::UClass* ClassOf(Menu m) {return m==Menu::kMagic?&magicClass:&mainClass;}
}
namespace container {
bool Open() {return containerOpen;}
UE::UObject* SideContent(int side) {return side==3?&yours:&theirs;}
UE::UObject* Widget() {return &yours;}
}
int walks=0,collections=0;
namespace ui {
struct Call {
 Call(UE::UObject*,const wchar_t*) {}
 explicit operator bool() const {return true;}
 bool Run() {++collections;return true;}
};
std::vector<UE::UClass*> TreeClasses(UE::UObject*,int) {++walks;return {&mainClass};}
}
namespace hoverfix { namespace {
void MenuMetric(UE::UObject*,UE::UFunction*,void*) {}
void TileMetric(UE::UObject*,UE::UFunction*,void*) {}
void SampleFrame(menus::Menu) {}
void Report(menus::Menu) {}
"""
suffix = r"""
} }
UE::UFunction hover, wrong;
std::vector<UE::UObject*> delivered;
bool nested=false, nestedWasSkipped=false;
bool rebuildStorm=false, crossSideStorm=false;
bool nonHoverAllowed=true;
void Dispatch(UE::UObject* o,UE::UFunction* f,void* p) {
 if (!hoverfix::Filter(o,f,p)) {
  delivered.push_back(o);
  if (rebuildStorm) {
   // Native row rebuilds emit more hover delegates during the delivered body.
   void* rebuilt=&itemC;
   hoverfix::Filter(o,f,&rebuilt);
   if (crossSideStorm) hoverfix::Filter(o==&yours?&theirs:&yours,f,&rebuilt);
   UE::UFunction focus;focus.name="OnFocusReceived";
   nonHoverAllowed &= !hoverfix::Filter(o,&focus,&rebuilt);
  }
  if (nested) {
   nested=false;
   void* item=&itemC;
   nestedWasSkipped=hoverfix::Filter(o,f,&item);
  }
 }
}
int main() {
 UE::dispatch=&Dispatch;
 mainClass.name.name="WBP_ModernMenu_ContainerInventory_MainContent_C";
 yours.cls=theirs.cls=inv.cls=&mainClass;
 magic.cls=&magicClass;
 hover.name="BndEvt__inv_cont_listview_K2Node_ComponentBoundEvent_OnItemHovered__DelegateSignature";
 wrong.name=hover.name;wrong.propertiesSize=16;
 int checks=0,failed=0;
 auto check=[&](bool ok,const char* why) {++checks;if(!ok){++failed;std::cout<<"FAIL "<<why<<"\n";}};
 void* a=&itemA;void* b=&itemB;
 check(!hoverfix::Filter(&yours,&hover,&a),"first player-side hover goes through");
 check(!hoverfix::Filter(&theirs,&hover,&b),"first merchant-side hover is independent");
 clockMs+=10;
 check(hoverfix::Filter(&yours,&hover,&b),"repeat player hover is throttled");
 check(hoverfix::Filter(&theirs,&hover,&a),"repeat merchant hover is throttled");
 clockMs+=201;
 hoverfix::Replay();
 check(std::count(delivered.begin(),delivered.end(),&yours)==1,"player pending event survives other-side activity");
 check(std::count(delivered.begin(),delivered.end(),&theirs)==1,"merchant pending event is replayed");
 check(!hoverfix::Filter(&yours,&wrong,&a),"unknown parameter layout fails open");
 // Delivering a deferred call must not let reentrant rebuild hovers bypass the gate.
 clockMs+=10;hoverfix::Filter(&yours,&hover,&a);
 clockMs+=201;nested=true;
 hoverfix::Replay();
 check(nestedWasSkipped,"replay bypass is single-use, nested row hovers remain throttled");
 // Weak liveness: a deferred item that has died must never reach ProcessEvent.
 nested=false;delivered.clear();clockMs+=10;
 hoverfix::Filter(&theirs,&hover,&b);itemB.live=false;clockMs+=201;
 hoverfix::Replay();
 check(std::count(delivered.begin(),delivered.end(),&theirs)==0,"dead deferred item is discarded");
 itemB.live=true;
 containerOpen=true;
 hoverfix::Discover(menus::Menu::kContainerYours);
 check(watches>0,"container-first opening installs hover hook without Inventory discovery");
 const int beforeWalks=walks;
 for(int i=0;i<1000;++i) hoverfix::Discover(menus::Menu::kContainerYours);
 check(walks==beforeWalks,"container hook discovery does not repeatedly walk the widget tree");

 // Context closure clears outstanding calls; guard remains enabled in gameplay.
 hoverfix::Tick(menus::Menu::kContainerYours);
 clockMs+=201;hoverfix::Filter(&yours,&hover,&a);
 clockMs+=10;hoverfix::Filter(&yours,&hover,&b);
 delivered.clear();containerOpen=false;
 hoverfix::Tick(menus::Menu::kNone);
 clockMs+=201;hoverfix::Replay();
 check(delivered.empty(),"no replay survives leaving the menu");
 clockMs+=201;containerOpen=true;settings.containerHover=false;
 check(!hoverfix::Filter(&yours,&hover,&a),"container throttle can be switched off");
 settings.containerHover=true;settings.inventoryHover=false;clockMs+=201;
 check(!hoverfix::Filter(&yours,&hover,&a),"container setting is independent of Inventory");
 clockMs+=10;
 check(hoverfix::Filter(&yours,&hover,&b),"enabled container throttle still holds repeat events");
 // Recycling a receiver must not replay its queued event.
 delivered.clear();++yours.serial;clockMs+=201;hoverfix::Replay();
 check(std::count(delivered.begin(),delivered.end(),&yours)==0,"recycled receiver discards pending hover");
 settings.hoverIntervalMs=0;
 check(!hoverfix::Filter(&yours,&hover,&a) && !hoverfix::Filter(&yours,&hover,&b),"zero interval passes events through");
 const auto now=Clock::now();
 UE::liveObjects=1600000;hoverfix::Guard(now+2s);
 check(collections==1,"guard collects above limit even outside Inventory/Magic");
 hoverfix::Guard(now+3s);
 check(collections==1,"guard cooldown prevents repeated collection");
 hoverfix::Guard(now+13s);
 check(collections==2,"guard can collect again after cooldown");
 UE::liveObjects=100000;hoverfix::Guard(now+24s);
 check(collections==2,"guard ignores low object count");
 // RMI-003 (formerly SO-019): both list receivers have deferred hovers; no further input arrives.
 // Each deferred native call rebuilds rows and synchronously emits more hovers.
 hoverfix::g_state.clear();settings.hoverIntervalMs=200;
 settings.inventoryHover=true;settings.containerHover=true;containerOpen=true;
 rebuildStorm=true;crossSideStorm=false;delivered.clear();
 clockMs+=201;Dispatch(&yours,&hover,&a);
 clockMs+=10;Dispatch(&theirs,&hover,&b);
 delivered.clear();clockMs+=201;
 hoverfix::Replay();
 check(delivered.size()==1 && delivered.back()==&theirs,
       "only the most recently used container side flushes its pending hover");
 const auto flushed=delivered.size();
 for(int i=0;i<10;++i){clockMs+=201;hoverfix::Replay();}
 check(delivered.size()==flushed,"idle replay cannot rearm itself via row rebuild hovers");
 check(nonHoverAllowed,"replay never blocks unrelated native focus events");
 rebuildStorm=false;delivered.clear();clockMs+=201;
 Dispatch(&theirs,&hover,&a);
 check(delivered.size()==1 && delivered.back()==&theirs,"real mouse/controller side change still reaches native body");
 clockMs+=10;Dispatch(&theirs,&hover,&b);
 delivered.clear();rebuildStorm=true;crossSideStorm=true;clockMs+=201;
 hoverfix::Replay();
 const auto crossFlushed=delivered.size();
 for(int i=0;i<10;++i){clockMs+=201;hoverfix::Replay();}
 check(delivered.size()==crossFlushed,"cross-side synthetic hovers cannot restart focus ping-pong");
 rebuildStorm=false;crossSideStorm=false;
 clockMs+=201;delivered.clear();Dispatch(&yours,&hover,&a);
 check(delivered.size()==1 && delivered.back()==&yours,"normal player-side hover still works after replay guard");
 std::cout<<checks<<" checks, "<<failed<<" failures\n";
 return failed?1:0;
}
"""
cpp=out/"hover_integration_tests.cpp"
cpp.write_text(prefix+throttle+extract("std::int64_t LiveObjects(")+extract("void Guard(")+extract("void Discover(")+extract("void Tick(")+suffix)
exe=out/"hover_integration_tests.exe"
subprocess.run(["cl","/nologo","/EHsc","/std:c++latest",str(cpp),
               f"/Fe:{exe}",f"/Fo:{out / 'hover_integration_tests.obj'}"],check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
