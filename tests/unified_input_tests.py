"""Exercise the real keyboard state machine and single driver with input/UE doubles."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
keyboard = (root / "searchkb/XInputHook.cpp").read_text(encoding="utf-8")
driver = (root / "sortingoverhaul/Driver.cpp").read_text(encoding="utf-8")


def extract(text, marker):
    start = text.index(marker)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


keyboard = "\n".join(line for line in keyboard.splitlines() if not line.startswith("#include"))
keyboard = keyboard.replace("using Clock = std::chrono::steady_clock;", "using Clock = FakeClock;")
prefix = r"""
#include <Windows.h>
#include <Xinput.h>
#include <chrono>
#include <cstdint>
#include <iostream>
#include "ControllerPolicy.h"
using namespace std::chrono_literals;
long long milliseconds=0;
struct FakeClock {
    using time_point=std::chrono::steady_clock::time_point;
    static time_point now() { return time_point{}+std::chrono::milliseconds(milliseconds); }
};
namespace menus {
enum class Menu { kNone,kInventory,kMagic,kContainerYours,kContainerTheirs,kQuests };
}
menus::Menu context=menus::Menu::kInventory, openedFor=menus::Menu::kNone;
bool keyboardOpen=false, popup=false;
int keyboardUses=0, pickerUses=0,ticks=0,publishes=0;
std::uint16_t closingMask=0;
namespace search { menus::Menu Context() { return context; } }
namespace kbcfg {
struct Config { bool enabled=true; int button=8,buttonLeft=4,holdMs=400; } config;
const Config& Get() { return config; }
}
namespace osk {
bool Taking() { return keyboardOpen; }
bool CanOpen(menus::Menu m) { return m!=menus::Menu::kNone; }
bool Open(menus::Menu m,std::uint16_t) { keyboardOpen=true; openedFor=m; return true; }
void Close(const char*) { keyboardOpen=false; }
void Rewrite(std::uint16_t buttons,short,short,FakeClock::time_point) {
    ++keyboardUses;
    if(buttons & (0x2000|0x10)) { keyboardOpen=false; closingMask=buttons; }
}
std::uint16_t Swallow(std::uint16_t raw) { closingMask &= raw; return closingMask; }
}
namespace sorting {
void Tick() { ++ticks; }
void PublishInputState() { ++publishes; }
}
namespace gearfilter {
bool PopupOpen() { return popup; }
void Controller(WORD& buttons,SHORT&,SHORT&,bool connected) {
    if(connected) { ++pickerUses; if(popup) buttons=0; }
}
}
"""
middle = r"""
namespace sortingdriver {
using XInputGetState_t=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
XINPUT_STATE native{};
DWORD nativeResult=ERROR_SUCCESS;
DWORD WINAPI Native(DWORD,XINPUT_STATE* state) { if(state) *state=native; return nativeResult; }
XInputGetState_t g_previous=&Native;
DWORD g_gameThread=GetCurrentThreadId();
sortpad::PacketStamp g_packets;
"""
tests = r"""
}
int main() {
    int checks=0,failed=0;
    const auto check=[&](bool ok,const char* why) {
        ++checks; if(!ok) { ++failed; std::cerr<<"FAIL "<<why<<"\n"; }
    };
    const auto read=[](WORD buttons,long long when) {
        milliseconds=when; sortingdriver::native.Gamepad={};
        sortingdriver::native.Gamepad.wButtons=buttons;
        XINPUT_STATE result{}; sortingdriver::Chained(0,&result); return result;
    };
    auto result=read(8,0);
    check(!(result.Gamepad.wButtons&8) && !keyboardOpen,"open gesture withheld while deciding tap/hold");
    result=read(0,100);
    check((result.Gamepad.wButtons&8) && !keyboardOpen,"short tap replayed to normal sorting/native path");
    read(0,110); read(0,120); read(0,130); read(0,140);
    read(8,200); const auto pickerBefore=pickerUses;
    result=read(8,700);
    check(keyboardOpen && openedFor==menus::Menu::kInventory,"normal long hold opens internal keyboard");
    check(pickerUses==pickerBefore,"opening sample is not also delivered to picker");
    sortingdriver::native.Gamepad.sThumbRX=22000;
    sortingdriver::native.Gamepad.bLeftTrigger=150;
    XINPUT_STATE pad{}; sortingdriver::Chained(0,&pad);
    check(!pad.Gamepad.wButtons && !pad.Gamepad.sThumbRX,"open keyboard owns pad buttons and sticks");
    check(pad.Gamepad.bLeftTrigger==150,"native tab-switch triggers retained while keyboard open");
    const auto uses=pickerUses;
    read(0x2000,710);
    check(!keyboardOpen && pickerUses==uses,"keyboard closing sample cannot also operate picker");
    result=read(0x2000,720);
    check(!(result.Gamepad.wButtons&0x2000),"held close button remains suppressed after keyboard closes");
    read(0,730);
    result=read(0x2000,740);
    check(result.Gamepad.wButtons&0x2000,"fresh close button works after release");
    popup=true;
    read(8,800); result=read(8,1400);
    check(!keyboardOpen && !result.Gamepad.wButtons,"picker popup has priority over keyboard gesture");
    popup=false; read(0,1500); context=menus::Menu::kContainerYours;
    read(4,1600); read(4,2100);
    check(keyboardOpen && openedFor==menus::Menu::kContainerTheirs,"left hold targets other container side");
    context=menus::Menu::kMagic; read(0,2200);
    check(!keyboardOpen,"context switch closes keyboard and resets hold");
    const auto beforeTicks=ticks;
    sortingdriver::nativeResult=ERROR_DEVICE_NOT_CONNECTED;
    sortingdriver::Chained(0,&pad);
    check(!keyboardOpen && ticks>beforeTicks,"disconnected controller does not stop menu tick");
    sortingdriver::nativeResult=ERROR_SUCCESS;
    const auto beforeOther=ticks;
    sortingdriver::native.Gamepad.wButtons=0x1000;
    sortingdriver::Chained(1,&pad);
    check(ticks==beforeOther && pad.Gamepad.wButtons==0x1000,"other users are passed through");
    sortpad::PacketStamp packets;
    auto first=packets.Update(1,{0,0,0,20000,0,100,0});
    auto released=packets.Update(1,{});
    check(first!=released,"synthetic right-stick/trigger release moves packet stamp");
    check(publishes>0,"driver publishes keyboard ownership for wheel readers");
    std::cout<<checks<<" checks, "<<failed<<" failures\n";
    return failed ? 1 : 0;
}
"""
cpp = out / "unified_input_tests.cpp"
cpp.write_text(prefix + keyboard + middle + extract(driver, "\t\tDWORD WINAPI Chained(")
               + tests, encoding="utf-8")
exe = out / "unified_input_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp),
                f"/I{root / 'sortingoverhaul'}", f"/Fe:{exe}",
                f"/Fo:{out / 'unified_input_tests.obj'}", "user32.lib"], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
