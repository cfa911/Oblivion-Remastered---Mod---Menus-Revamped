"""Run the real menu post-event watcher (OnMenuPost, FilterQuick) against doubles: pure input events (mouse moves,
navigation queries) cannot change a list and must not filter both container sides, every other native event still
filters right away (no flash after a rebuild), and an event's name is read once per UFunction, not per call."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
source = source_paths.read("search")
classifier = source_paths.function(source, "\t\tenum class ListEvent")
bodies = (classifier + (";" if classifier else "") + "\n"
          + source_paths.function(source, "\t\tListEvent ClassifyEvent(") + "\n"
          + source_paths.function(source, "\tstatic void FilterQuick(") + "\n"
          + source_paths.function(source, "\tstatic void OnMenuPost("))

prefix = r"""
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
using Clock = std::chrono::steady_clock;
namespace UE {
struct UClass {};
struct UObject { UClass* cls; UClass* GetClass() { return cls; } };
struct UFunction { std::string name; };
}
UE::UClass containerClass, inventoryClass, magicClass, pageClass, panelClass;
namespace menus {
enum class Menu { kNone, kInventory, kMagic, kContainerYours, kContainerTheirs, kQuests };
UE::UClass* ClassOf(Menu m) { return m == Menu::kInventory ? &inventoryClass : m == Menu::kMagic ? &magicClass : nullptr; }
}
namespace container { UE::UClass* Class() { return &containerClass; } }
int noted = 0;
namespace quests {
UE::UClass* PageClass() { return &pageClass; }
UE::UClass* PanelClass() { return &panelClass; }
void NoteInstance(UE::UObject*) { ++noted; }
}
int names = 0;
namespace pe { std::string FunctionName(UE::UFunction* f) { ++names; return f->name; } }
int filtered[6]{};
bool nativeSchoolVisible=true, nativeTypeVisible=true;
int magicHeaderRefreshes=0;
namespace sorting {
void RefreshMagicHeaders(UE::UObject*) {
    ++magicHeaderRefreshes;
    nativeSchoolVisible=nativeTypeVisible=false;
}
}
struct Lifecycle { bool nativeDirty = false; };
struct State { Lifecycle lifecycle; };
namespace search {
State g_filter[6];
bool g_filtering = false;
void FilterTick(menus::Menu m, Clock::time_point) {
    ++filtered[int(m)];
    if(m==menus::Menu::kMagic) nativeSchoolVisible=nativeTypeVisible=true;
}
"""

tests = r"""
}
int main() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::cout << "FAIL: " << why << "\n"; } };
    UE::UObject menu{ &containerClass }, inventory{ &inventoryClass }, page{ &pageClass };
    UE::UFunction move{ "OnMouseMove" }, wheel{ "OnMouseWheel" }, navigable{ "IsNavigable" }, layer{ "GetWidgetLayer" },
        enter{ "OnMouseEnter" }, leave{ "OnMouseLeave" }, tick{ "Tick" }, update{ "UpdateItemList" },
        view{ "UpdateInventoryListView" };
    for (int i = 0; i < 100; ++i)
        for (auto* f : { &move, &wheel, &navigable, &layer, &enter, &leave }) search::OnMenuPost(&menu, f, nullptr);
    check(filtered[3] == 0 && filtered[4] == 0, "pure input events on the container filter neither side");
    check(names <= 6, "each event's name is read once per UFunction");
    search::OnMenuPost(&menu, &tick, nullptr);
    check(filtered[3] == 1 && filtered[4] == 1, "a frame event still filters both sides right after it");
    search::OnMenuPost(&menu, &view, nullptr);
    check(filtered[3] == 2 && filtered[4] == 2, "a list rebuild filters both sides right after it");
    check(!search::g_filter[3].lifecycle.nativeDirty, "only UpdateItemList marks a new generation (unchanged)");
    search::OnMenuPost(&menu, &update, nullptr);
    check(search::g_filter[3].lifecycle.nativeDirty && search::g_filter[4].lifecycle.nativeDirty && filtered[3] == 3, "UpdateItemList marks and filters both sides");
    search::OnMenuPost(&inventory, &move, nullptr);
    check(filtered[1] == 0, "pure input events on the Inventory do not filter");
    search::OnMenuPost(&inventory, &update, nullptr);
    check(filtered[1] == 1 && search::g_filter[1].lifecycle.nativeDirty, "the Inventory's rebuild filters it");
    UE::UObject magic{ &magicClass };
    auto beforeHeaders=magicHeaderRefreshes;
    search::OnMenuPost(&magic, &update, nullptr);
    check(magicHeaderRefreshes==beforeHeaders+1 && !nativeSchoolVisible && !nativeTypeVisible,
          "Magic rebuild re-hides replaced native sort icons before the post-event watcher returns");
    beforeHeaders=magicHeaderRefreshes;
    search::OnMenuPost(&magic, &view, nullptr);
    check(magicHeaderRefreshes==beforeHeaders+1 && !nativeSchoolVisible && !nativeTypeVisible,
          "header hiding follows the native/filter refresh rather than waiting for next frame");
    beforeHeaders=magicHeaderRefreshes;
    search::OnMenuPost(&magic, &move, nullptr);
    check(magicHeaderRefreshes==beforeHeaders,"pure Magic input queries do not add header work");
    search::OnMenuPost(&inventory, &view, nullptr);
    check(magicHeaderRefreshes==beforeHeaders,"Inventory events do not change Magic headers");
    search::OnMenuPost(&page, &move, nullptr);
    check(noted == 1 && filtered[5] == 0, "the quests page is still noted on any event, input events do not filter");
    search::OnMenuPost(&page, &tick, nullptr);
    check(filtered[5] == 1, "other quests events filter");
    search::g_filtering = true;
    search::OnMenuPost(&menu, &tick, nullptr);
    check(filtered[3] == 3, "nothing runs while the filter itself is running");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
"""
cpp = out / "menu_post_events_tests.cpp"
cpp.write_text(prefix + bodies + tests, encoding="utf-8")
exe = out / "menu_post_events_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp), *source_paths.include_flags(),
                f"/Fe:{exe}", f"/Fo:{out / 'menu_post_events_tests.obj'}"], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
