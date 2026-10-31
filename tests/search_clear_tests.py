"""Run the real search Tick and keyboard Open bodies against menu/UI doubles."""
from pathlib import Path
import subprocess
import sys
import source_paths

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
tick = source_paths.function((root / 'src/search/Search.cpp').read_text(), 'void Tick(')
opening = source_paths.function((root / 'src/keyboard/Keyboard.cpp').read_text(), 'bool Open(')
prefix = r'''
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
namespace menus {
enum class Menu { kNone, kInventory, kMagic, kContainerYours, kContainerTheirs, kQuests };
bool menuMode = false;
bool AnyOpen() { return menuMode; }
const char* Name(Menu) { return "menu"; }
}
namespace logger { template<class... T> void debug(T&&...) {} }
namespace searchcfg {
struct Values { bool clearTextOnGameplay = true; } config;
const Values& Get() { return config; }
}
namespace kbcfg {
struct Values { bool clearTextOnActivation = true; } config;
const Values& Get() { return config; }
}
namespace container { bool open=false; bool Open() { return open; } }
namespace quests { bool open=false; bool Open() { return open; } }
namespace search {
std::string queries[6], visible[6];
std::atomic<bool> g_sideShown[6], g_typing{false}, g_external{false};
std::atomic<int> g_typingMenu{0};
bool enabled=true;
bool IsSide(menus::Menu m) { return m==menus::Menu::kContainerYours || m==menus::Menu::kContainerTheirs; }
bool SideShownNow(menus::Menu) { return true; }
bool Enabled(menus::Menu) { return enabled; }
bool PanelOnLeft(menus::Menu) { return true; }
void SetQuery(menus::Menu m, const std::string& text) { queries[int(m)]=text; }
void SetText(menus::Menu m, const std::string& text) { SetQuery(m,text); }
std::string GetText(menus::Menu m) { return queries[int(m)]; }
void SetExternalInput(bool b) { g_external=b; }
void BarTick(menus::Menu, Clock::time_point) {}
void FilterTick(menus::Menu m, Clock::time_point) { visible[int(m)]=queries[int(m)]; }
'''
middle = r'''
}
namespace reflect { int Get(int h) { return h; } }
namespace ui {
struct Call { Call(int,const wchar_t*) {} void Run() {} };
void Visible(int,bool) {}
}
namespace keyboard {
bool g_open=false, g_mirror=false, g_builtMirror=false, buildOk=true;
int g_root=0,g_header=0,g_lastBuild=0,g_sel=0,g_col=0;
std::vector<int> g_keyImg;
std::string g_text;
menus::Menu g_menu=menus::Menu::kNone;
std::uint16_t g_ignore=0,g_prevEff=0,g_prevWant=0,g_swallow=0;
constexpr std::size_t kMaxText=40;
bool EnsureBuilt() { return buildOk; }
void Paint(int,bool) {} void Select(int) {} int KeyAt(int,int) { return 0; }
void ShowText() {}
'''
main = r'''
}
int main() {
using menus::Menu;
int checks=0,failed=0;
auto check=[&](bool ok,const char* why) { ++checks; if(!ok) { ++failed; std::cerr<<"FAIL "<<why<<"\n"; } };
auto fill=[] { for(int i=1;i<=5;++i) search::queries[i]="old"; };
auto allEmpty=[] { for(int i=1;i<=5;++i) if(!search::queries[i].empty()) return false; return true; };
search::Tick(Menu::kNone);
fill(); menus::menuMode=true; search::Tick(Menu::kInventory);
search::Tick(Menu::kMagic);
check(search::queries[1]=="old" && search::queries[2]=="old","switching Inventory/Magic retains searches");
search::Tick(Menu::kNone);
check(search::queries[1]=="old","another in-game menu page does not count as gameplay");
quests::open=true; search::Tick(Menu::kNone);
quests::open=false; menus::menuMode=false; search::Tick(Menu::kNone);
check(allEmpty(),"return from Quest/menu mode clears all stored searches");
menus::menuMode=true; search::Tick(Menu::kInventory); fill();
menus::menuMode=false; search::Tick(Menu::kNone);
check(allEmpty(),"return from Inventory clears searches");
menus::menuMode=true; search::Tick(Menu::kMagic); fill();
menus::menuMode=false; search::Tick(Menu::kNone);
check(allEmpty(),"return from Magic clears searches");
search::queries[1]="after"; search::Tick(Menu::kNone);
check(search::queries[1]=="after","idle gameplay does not repeatedly clear");
searchcfg::config.clearTextOnGameplay=false; fill();
menus::menuMode=true; search::Tick(Menu::kInventory);
menus::menuMode=false; search::Tick(Menu::kNone);
check(search::queries[1]=="old" && search::queries[5]=="old","gameplay clearing can be disabled");
fill(); check(keyboard::Open(Menu::kMagic,8),"keyboard opens");
check(keyboard::g_text.empty() && search::queries[2].empty(),"activation clears both keyboard and target search");
check(search::queries[1]=="old","keyboard activation preserves other searches");
menus::menuMode=true; search::Tick(Menu::kMagic);
check(search::visible[2].empty(),"next search tick applies cleared filter");
search::queries[2]="new"; keyboard::Open(Menu::kMagic,8);
check(search::queries[2]=="new","already open call does not clear edits again");
keyboard::g_open=false; keyboard::buildOk=false; fill();
check(!keyboard::Open(Menu::kMagic,8) && search::queries[2]=="old","failed keyboard build preserves search");
keyboard::buildOk=true; search::enabled=false;
check(!keyboard::Open(Menu::kMagic,8) && search::queries[2]=="old","disabled search is not cleared");
search::enabled=true; kbcfg::config.clearTextOnActivation=false;
keyboard::Open(Menu::kMagic,8);
check(keyboard::g_text=="old" && search::queries[2]=="old","keyboard clearing can be disabled independently");
keyboard::g_open=false; kbcfg::config.clearTextOnActivation=true;
keyboard::Open(Menu::kContainerTheirs,4);
check(search::queries[4].empty() && search::queries[3]=="old","container keyboard clears only targeted side");
std::cout<<checks<<" checks, "<<failed<<" failures\n";
return failed ? 1 : 0;
}
'''
cpp = out / 'search_clear_tests.cpp'
cpp.write_text(prefix + tick + middle + opening + main, encoding='utf-8')
exe = out / 'search_clear_tests.exe'
subprocess.run(['cl','/nologo','/EHsc','/std:c++latest',str(cpp),f'/Fe:{exe}',f'/Fo:{out}/'],check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
