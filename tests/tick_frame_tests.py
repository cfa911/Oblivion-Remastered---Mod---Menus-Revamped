"""Run the real plugin tick (core::Tick) and container discovery against doubles: the window timer and the controller
read often tick in the same engine frame and the second tick must do nothing; one tick reads the container menu's and
each side's visibility once (a parent walk of IsVisible calls each), outside a tick every read stays fresh, and a
menu closed in the middle of a tick is honoured at once."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
strip = lambda unit: "\n".join(l for l in source_paths.read(unit).splitlines() if not l.startswith("#include"))
side_shown = source_paths.function(source_paths.read("search"), "\tbool SideShownNow(")

prefix = r"""
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include "TransferHold.h"
namespace UE {
struct UObject { bool live = true; int serial = 1; };
struct UClass : UObject {};
struct UFunction { std::string name; };
UClass cls;
template<class T> T* StaticFindObject(void*, void*, const wchar_t*) { return &cls; }
}
UE::UObject menu, yours, theirs;
namespace reflect {
struct Handle { UE::UObject* ptr = nullptr; int index = 0; };
bool Ok() { return true; }
bool SelfCheck() { return true; }
Handle Hold(UE::UObject* o) { return { o, o ? o->serial : 0 }; }
UE::UObject* Get(const Handle& h) { return h.ptr && h.ptr->live && h.ptr->serial == h.index ? h.ptr : nullptr; }
std::vector<UE::UObject*> Instances(UE::UClass*) { return { &menu }; }
}
#include "engine/ContainerMenu.h"
#include "core/Runtime.h"
namespace menus {
bool AnyOpen() { return true; }
void Tick() {}
Menu Active() { return Menu::kNone; }
UE::UObject* Widget(Menu) { return nullptr; }
}
namespace logger { template<class... T> void info(T&&...) {} }
namespace pe {
std::string FunctionName(UE::UFunction* f) { return f->name; }
template<class F> bool Watch(UE::UClass*, F) { return true; }
}
std::map<UE::UObject*, int> walks;
double clockNow = 1.0;
namespace ui {
bool ShownOnScreen(UE::UObject* o) { ++walks[o]; return o != nullptr; }
UE::UObject* FindInTree(UE::UObject*, std::string_view name) { return name == "cont_give_maincontent" ? &yours : &theirs; }
UE::UObject* TreeRootCanvas(UE::UObject*) { return nullptr; }
float ViewportScale() { return 1; }
double RealTimeSeconds() { return clockNow; }
}
namespace mrconfig { struct Settings { bool returnToListAfterFilter = false; }; Settings s; const Settings& Get() { return s; } }
namespace quests { void Tick() {} }
namespace keyboard { bool Taking() { return false; } void Close(const char*, std::uint16_t = 0) {} }
namespace hoverfix { void Tick(menus::Menu) {} }
"""
middle = r"""
namespace sorting {
enum class Mode { None, Inventory, Magic, Container };
int ticks = 0;
bool closeDuringTick = false;
void Tick(Mode, UE::UObject*, UE::UObject*, float, UE::UObject* = nullptr);
bool PopupOpen() { return false; }
bool Message(UINT, WPARAM, LPARAM) { return false; }
}
namespace search {
""" + side_shown + r"""
void Tick(menus::Menu) {   // as the real one: both sides' visibility, then the filter's presence checks
    for (auto side : { menus::Menu::kContainerYours, menus::Menu::kContainerTheirs }) { SideShownNow(side); container::Open(); }
}
void SetWindow(HWND) {}
bool HandleMessage(HWND, UINT, WPARAM, LPARAM, LRESULT&) { return false; }
menus::Menu Context() { return menus::Menu::kContainerYours; }
}
void sorting::Tick(Mode, UE::UObject*, UE::UObject*, float, UE::UObject*) {
    ++ticks;
    if (closeDuringTick) { UE::UFunction close{ "BP_OnDeactivated" }; container::OnEvent(&menu, &close, nullptr); }
}
"""
tests = r"""
int main() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::cout << "FAIL: " << why << "\n"; } };
    container::Tick();   // discovery reads the menu once, outside any tick
    walks.clear();
    core::Tick();
    check(sorting::ticks == 1, "the first tick of a frame runs");
    check(walks[&menu] == 1, "one tick reads the container menu's visibility once");
    check(walks[&yours] == 1 && walks[&theirs] == 1, "one tick reads each side's visibility once");
    core::Tick();
    check(sorting::ticks == 1, "a second tick in the same engine frame does nothing");
    clockNow = 1.016;
    core::Tick();
    check(sorting::ticks == 2 && walks[&menu] == 2, "the next frame ticks and reads visibility again");
    clockNow = -1.0;   // no world yet: the clock cannot be read
    core::Tick(); core::Tick();
    check(sorting::ticks == 4, "without the engine clock every tick runs");
    walks.clear();
    for (int i = 0; i < 5; ++i) container::Open();
    check(walks[&menu] == 5, "outside a tick every visibility read is fresh");
    clockNow = 2.0; sorting::closeDuringTick = true; walks.clear();
    core::Tick();
    check(!container::Open() && walks[&menu] == 1, "a menu closed inside a tick is closed at once");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
"""
cpp = out / "tick_frame_tests.cpp"
cpp.write_text(prefix + strip("container") + middle + strip("runtime") + tests, encoding="utf-8")
exe = out / "tick_frame_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp), *source_paths.include_flags(),
                f"/Fe:{exe}", f"/Fo:{out / 'tick_frame_tests.obj'}", "user32.lib"], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
