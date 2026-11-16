"""C4-8 (owner 2026-11-16: "add the feature of the controller keyboard on this screen, the one we added on the alchemy"):
run the real Enchanting name tick (src/enchanting/EnchantingName.cpp EnchantNameTick) with native-text and keyboard
doubles. The Enchanting menu keeps the item name only in EnchantedItem_NameBox (read by "On Enchant Requested"); its own
UpdateRenameBoxFocus(New Focus State) starts/stops editing (static kismet_dump, build 1.512.105.0).
Usage: python tests/enchant_name_bridge_tests.py <build folder> (needs cl on PATH)."""
from pathlib import Path
import subprocess
import sys
import source_paths

out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/tests").resolve()
out.mkdir(parents=True, exist_ok=True)
prefix = r'''
#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
namespace UE { struct UObject {}; }
namespace menus { enum class Menu { kEnchantName, kSpellName }; }
menus::Menu searched=menus::Menu::kEnchantName;
std::string nativeName="Old Ring",query;
std::vector<std::wstring> calls;
std::vector<bool> focusCalls;
bool enabled=true,visible=true,build=true,taking=false,modeBeforeOpen=false;
std::atomic<bool> g_canName{false},g_nameMode{false},g_nameKeyboardRequest{false};
std::string g_nameApplied,g_nameInitial;
constexpr std::uint16_t kPadName=0x40;
namespace ui {
bool ShownOnScreen(UE::UObject*) { return visible; }
struct Call {
std::wstring fn;
Call(UE::UObject*,const wchar_t* f):fn(f) {}
explicit operator bool() const { return true; }
bool Run() { calls.push_back(fn); return true; }
template<class T> T Get(const char*) { return enabled; }
};
}
namespace logger { template<class... T> void warn(T&&...) {} template<class... T> void debug(T&&...) {} }
namespace search { void SetText(menus::Menu m,const std::string& s) { searched=m; query=s; } std::string GetText(menus::Menu) { return query; } }
namespace keyboard {
bool Taking() { return taking; }
bool Open(menus::Menu m,std::uint16_t) { modeBeforeOpen=g_nameMode.load(); taking=build; return taking; }
}
std::string ReadName(UE::UObject*) { return nativeName; }
bool NameTextCall(UE::UObject*,const wchar_t* fn,const char*,const std::string& value,bool=false) { calls.push_back(fn); nativeName=value; return true; }
bool RenameFocus(UE::UObject*,bool on) { focusCalls.push_back(on); return true; }
'''
main = r'''
int main() {
int checks=0,failed=0;
auto check=[&](bool ok,const char* why) { ++checks; if(!ok) { ++failed; std::cerr<<"FAIL "<<why<<"\n"; } };
UE::UObject menu,box;
g_nameKeyboardRequest=true; EnchantNameTick(&menu,&box,menus::Menu::kEnchantName);
check(taking && g_nameMode && modeBeforeOpen,"the name context is published before the keyboard opens");
check(query=="Old Ring","opening loads the box's current text");
check(focusCalls.size()==1 && focusCalls[0],"opening starts editing through the menu's UpdateRenameBoxFocus(true)");
query="Ring of Test"; calls.clear(); EnchantNameTick(&menu,&box,menus::Menu::kEnchantName);
check(nativeName=="Ring of Test","live edits reach the name box");
check(calls.size()==3 && calls[1]==L"SetUserInputText" && calls[2]==L"UpdateOnTextChanged","editing uses the wrapper setter and change route");
taking=false; calls.clear(); focusCalls.clear(); EnchantNameTick(&menu,&box,menus::Menu::kEnchantName);
check(!g_nameMode && nativeName=="Ring of Test","closing keeps the entered name and releases the name context");
check(!calls.empty() && calls.back()==L"Update on Text Committed","closing commits the text");
check(focusCalls.size()==1 && !focusCalls[0],"closing stops editing through UpdateRenameBoxFocus(false)");
build=false; g_nameKeyboardRequest=true; focusCalls.clear(); EnchantNameTick(&menu,&box,menus::Menu::kEnchantName);
check(!g_nameMode && !taking && focusCalls.size()==2 && !focusCalls[1],"a failed keyboard build unwinds the native editing");
build=true; visible=false; g_nameKeyboardRequest=true; focusCalls.clear(); EnchantNameTick(&menu,&box,menus::Menu::kEnchantName);
check(!g_nameMode && !taking && !g_canName && focusCalls.empty(),"a hidden name box cannot open the keyboard");
visible=true; nativeName="Old Spell"; g_nameKeyboardRequest=true; focusCalls.clear(); EnchantNameTick(&menu,&box,menus::Menu::kSpellName);
check(taking && searched==menus::Menu::kSpellName && query=="Old Spell","Spellmaking: the same flow targets the spell name");
taking=false; EnchantNameTick(&menu,&box,menus::Menu::kSpellName);
check(!g_nameMode && focusCalls.size()==2 && !focusCalls[1],"Spellmaking: closing stops editing");
std::cout<<checks<<" checks, "<<failed<<" failures\n";
return failed ? 1 : 0;
}
'''
root = source_paths.ROOT
wiring = [
    ("src/engine/Menus.h", "kEnchantName", "the Enchanting name has its own keyboard target"),
    ("src/search/Search.cpp", "enchanting::NamingMenu()", "the active search target is the altar name while naming"),
    ("src/search/SearchState.cpp", "enchanting::NamingMenu() == a_menu", "the name target is available only while naming"),
    ("src/core/Runtime.cpp", "enchanting::Tick();", "the name tick runs every frame"),
    ("src/core/Input.cpp", "enchanting::Controller(", "L3 reaches the Enchanting controller hook"),
    ("src/keyboard/Keyboard.cpp", "Item name: ", "the keyboard titles the field Item name"),
    ("src/keyboard/Keyboard.cpp", "Spell name: ", "the keyboard titles the Spellmaking field Spell name"),
    ("src/enchanting/EnchantingName.cpp", "SpellMakingItem_NameBox", "the Spellmaking altar's name box is handled too (owner 2026-11-16)"),
    ("src/enchanting/EnchantingName.cpp", "WBP_ModernMenu_SigilStoneMenu", "the Sigil Stone menu's name box is handled too (C4-11)"),
]
wiring_fails = [why for path, marker, why in wiring if not (root / path).exists() or marker not in (root / path).read_text(encoding="utf-8-sig")]
for why in wiring_fails:
    print("FAIL", why)
try:
    body = source_paths.function(source_paths.read("enchanting"), "void EnchantNameTick(")
except (FileNotFoundError, ValueError, IndexError) as error:
    print(f"FAIL EnchantNameTick not found ({type(error).__name__}: {error})")
    print(f"{len(wiring) + 1} checks, {len(wiring_fails) + 1} failures")
    sys.exit(1)
cpp = out / "enchant_name_bridge_tests.cpp"
cpp.write_text(prefix + body + main, encoding="utf-8")
exe = out / "enchant_name_bridge_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp), f"/Fe:{exe}", f"/Fo:{out}/"], check=True, capture_output=True)
code = subprocess.run([str(exe)]).returncode
print(f"wiring: {len(wiring)} checks, {len(wiring_fails)} failures")
sys.exit(1 if code or wiring_fails else 0)
