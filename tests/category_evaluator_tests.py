"""Actual category evaluator body: generations, native side indices and pending results."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
source = source_paths.read("picker")


def extract(marker, semicolon=False):
    start = source.index(marker)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + (";" if semicolon else "")


prefix = r"""
#include <Windows.h>
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <utility>
#include <vector>
namespace UE { struct UObject { int index=0; std::uint32_t form=0; std::uint64_t key=0; int stolen=0; }; }
#if __has_include("StolenFilter.h")
#include "StolenFilter.h"
#else
namespace stolenfilter { enum class Status : signed char { Unknown=-1, Legal=0, Stolen=1 }; }
#endif
using Rows=std::vector<UE::UObject*>;
std::uint64_t now=1000;
#define GetTickCount64() now
namespace logger { template<class... T> void warn(T...) {} }
namespace geardata {
struct Entry { int index; std::uint32_t tags; bool valid; };
struct RequestInfo { unsigned slot; std::vector<int> indices,native; };
std::uint64_t next=0;
std::vector<RequestInfo> requests;
std::unordered_map<std::uint64_t,std::vector<Entry>> replies;
unsigned cancelled[3]{};
void Cancel(unsigned slot) { ++cancelled[slot]; }
std::uint64_t Request(std::vector<int> indices,std::vector<std::uint32_t>,
                     unsigned slot,std::vector<int> native) {
    requests.push_back({slot,indices,native}); return ++next;
}
bool Take(std::uint64_t ticket,std::vector<Entry>& entries) {
    auto found=replies.find(ticket); if(found==replies.end()) return false;
    entries=std::move(found->second); replies.erase(found); return true;
}
}
namespace sorting {
"""
middle = r"""
FilterState g_states[3];
std::unordered_map<std::uint64_t,std::uint64_t> g_rowTags;
struct Picker {
    int kind=0; unsigned choice=0,appliedChoice=0,category=0,appliedCategory=0;
    bool stolenOnly=false,appliedStolen=false;
    bool Passes(std::uint64_t mask) const {
        if(kind==5) return !choice || !(mask & ((1ull<<28)|(1ull<<29)|(1ull<<30))) ||
            (mask & (choice==1 ? (1ull<<29) : choice==2 ? (1ull<<30) : (1ull<<28)));
        return !category || (mask & category)!=0;
    }
} g_pickers[5];
bool active=true;
bool Active(int) { return active; }
int Index(UE::UObject* item) { return item->index; }
std::uint32_t ContainerForm(UE::UObject* item,std::uint64_t* key=nullptr) {
    if(key) *key=item->key ? item->key : item->form; return item->form;
}
int stolenReads=0;
stolenfilter::Status StolenStatus(UE::UObject* item) {
    ++stolenReads; return static_cast<stolenfilter::Status>(item->stolen);
}
void ComputeMagicMasks(const Rows& items,FilterState& state) {
    for(auto* item:items) state.masks[item->index]=item->form;
}
"""
tests = r"""
}
int main() {
    int checks=0,failed=0;
    const auto check=[&](bool ok,const char* why) {
        ++checks; if(!ok) { ++failed; std::cerr<<"FAIL "<<why<<"\n"; }
    };
    UE::UObject first{7,1},second{9,2};
    Rows source{&first,&second};
    sorting::g_pickers[2].category=1;
    check(sorting::Evaluate(3,source,1)==source,"pending classification fails open using native rows");
    check(geardata::requests.back().slot==1 &&
          geardata::requests.back().indices==std::vector<int>({0,1}) &&
          geardata::requests.back().native==std::vector<int>({7,9}),
          "player request preserves source positions and exact native indices");
    geardata::replies[1]={{0,1,true},{1,2,true}};
    check(sorting::Evaluate(3,source,1)==Rows{&first},"ready tags filter player category");
    sorting::Evaluate(3,source,1);
    check(geardata::requests.size()==1,"same generation never requeues consumed classification");
    geardata::replies[1]={{0,1,true},{1,2,true}}; // stale old generation
    first.form=3; second.form=4; // reused wrappers now hold other items
    check(sorting::Evaluate(3,source,2)==source,"reused wrappers start new generation rather than old tag projection");
    check(sorting::g_states[1].ticket==2,"new generation owns a different classification ticket");
    geardata::replies[2]={{0,2,true},{1,1,true}};
    check(sorting::Evaluate(3,source,2)==Rows{&second},"only current generation reply affects category");
    sorting::g_pickers[3].category=2;
    UE::UObject m1{7,60},m2{9,61};
    Rows merchant{&m1,&m2};
    sorting::Evaluate(4,merchant,1);
    check(geardata::requests.back().slot==2,"merchant has its own request slot");
    geardata::replies[3]={{0,2,true},{1,0,false}};
    check(sorting::Evaluate(4,merchant,1)==merchant,"unresolved merchant rows fail open");
    check(sorting::Evaluate(3,source,2)==Rows{&second},"merchant state cannot replace player tags");
    sorting::Evaluate(4,merchant,2);
    now+=3000;
    check(sorting::Evaluate(4,merchant,2)==merchant && !sorting::g_states[2].ticket,
          "timeout cancels pending request and leaves native rows usable");
    const auto count=geardata::requests.size();
    sorting::Evaluate(4,merchant,2);
    check(geardata::requests.size()==count,"timed-out generation cannot requeue forever");
    // RMI-005: a rebuild after a sale/deposit must not show rows already known to fail the category.
    // Container/barter rows have null forms (docs/inventory/internals.md section 4): only the row key identifies them.
    UE::UObject keep{20,0,50},drop{21,0,51},fresh{22,0,52};
    sorting::Evaluate(3,Rows{&keep,&drop},10);
    geardata::replies[geardata::next]={{0,1,true},{1,2,true}};
    check(sorting::Evaluate(3,Rows{&keep,&drop},10)==Rows{&keep},"classified player rows filter");
    check(sorting::Evaluate(3,Rows{&drop,&keep},11)==Rows{&keep},
          "rebuild keeps known rows filtered while the new request is pending");
    check(sorting::Evaluate(3,Rows{&drop,&keep,&fresh},12)==Rows{&keep,&fresh},
          "rows never classified stay visible while pending");
    UE::UObject sold{30,0,51};
    check(sorting::Evaluate(4,Rows{&sold},20)==Rows{&sold},"merchant category 2 keeps the sold row");
    sorting::g_pickers[3].category=1;
    check(sorting::Evaluate(4,Rows{&sold},21).empty(),"row moved to the other side uses its known tags at once");
    geardata::replies[geardata::next]={{0,1,true}};
    check(sorting::Evaluate(4,Rows{&sold},21)==Rows{&sold},"fresh reply replaces carried tags");
    sorting::g_pickers[3].category=2;
    const auto magicCount=geardata::requests.size();
    sorting::g_pickers[0].kind=1; sorting::g_pickers[0].category=1;
    check(sorting::Evaluate(2,source,1)==Rows{&first},"Magic category tags remain supported");
    check(geardata::requests.size()==magicCount,"Magic does not use inventory/container request path");
    // Magic type picker combines with School/Effect and invalidates cached results immediately.
    sorting::g_pickers[0].category=0; sorting::g_pickers[1].kind=2;
    sorting::g_pickers[4].kind=5;
    UE::UObject spell{70,1u<<29},scroll{71,1u<<30},power{72,(1u<<28)|1u};
    Rows magicRows{&spell,&scroll,&power};
    check(sorting::Evaluate(2,magicRows,22)==magicRows,"Magic type All keeps both kinds");
    sorting::g_pickers[4].choice=1;
    check(sorting::Evaluate(2,magicRows,22)==Rows{&spell},"Spells excludes scrolls and powers immediately without a new generation");
    sorting::g_pickers[4].choice=2;
    check(sorting::Evaluate(2,magicRows,22)==Rows{&scroll},"Scrolls excludes spells and powers");
    sorting::g_pickers[0].category=1;
    sorting::g_pickers[4].choice=3;
    check(sorting::Evaluate(2,magicRows,22)==Rows{&power},"type combines with existing School filter");
    sorting::g_pickers[0].category=0;
    sorting::g_pickers[4].choice=0;
    check(sorting::Evaluate(2,magicRows,22)==magicRows,"All restores original complete Magic source");
    sorting::g_pickers[4].choice=2;
    check(sorting::Evaluate(1,magicRows,23)==magicRows,"Magic type selection cannot filter Inventory");
    sorting::g_pickers[4].kind=-1;
    // RMI-015: Inventory sorting must not expose known excluded items while TES tags refresh.
    sorting::g_pickers[0].kind=0; sorting::g_pickers[0].category=1;
    UE::UObject armor{80,100,1000},weapon{81,101,1001},newItem{82,102,1002};
    Rows gear{&armor,&weapon};
    sorting::Evaluate(1,gear,100);
    geardata::replies[geardata::next]={{80,1,true},{81,2,true}};
    check(sorting::Evaluate(1,gear,100)==Rows{&armor},"Inventory category initially excludes nonmatching items");
    UE::UObject sortedWeapon{91,101,1001},sortedArmor{90,100,1000};
    check(sorting::Evaluate(1,Rows{&sortedWeapon,&sortedArmor},101)==Rows{&sortedArmor},
        "Inventory sort keeps known categories filtered across new wrappers and indices");
    check(sorting::Evaluate(1,Rows{&sortedWeapon,&newItem,&sortedArmor},102)==Rows({&newItem,&sortedArmor}),
        "Inventory refresh fails open only for genuinely unknown items");
    geardata::replies[geardata::next]={{91,1,true},{82,2,true},{90,1,true}};
    check(sorting::Evaluate(1,Rows{&sortedWeapon,&newItem,&sortedArmor},102)==Rows({&sortedWeapon,&sortedArmor}),
        "fresh classification replaces carried Inventory tags");
    sorting::g_pickers[0].category=0;
    check(sorting::Evaluate(1,Rows{&sortedWeapon,&newItem,&sortedArmor},103)==Rows({&sortedWeapon,&newItem,&sortedArmor}),
        "Inventory All retains the complete native list during sorting");
    sorting::g_pickers[0].category=2;
    check(sorting::Evaluate(1,Rows{&sortedWeapon,&newItem,&sortedArmor},103)==Rows{&newItem},
        "changing category while classification is pending uses the known tags");
    sorting::g_pickers[0].category=0;
    // Stolen-only toggle (owner 2026-10-29): StatusFlags bit 0, Inventory and the player's side only.
    for(auto& picker:sorting::g_pickers) { picker.category=0; picker.stolenOnly=false; }
    sorting::g_pickers[0].kind=0; sorting::g_pickers[2].kind=3; sorting::g_pickers[3].kind=4;
    UE::UObject legal{40,10,0,0},stolen{41,11,0,1},stolenGear{42,12,0,1},unknown{43,13,0,-1};
    Rows inventory{&legal,&stolen,&stolenGear,&unknown};
    sorting::Evaluate(1,inventory,30);
    geardata::replies[geardata::next]={{40,1,true},{41,1,true},{42,2,true},{43,1,true}};
    sorting::stolenReads=0;
    check(sorting::Evaluate(1,inventory,30)==inventory,"toggle off keeps legal rows");
    check(sorting::stolenReads==0,"toggle off never reads row status");
    sorting::g_pickers[0].stolenOnly=true;
    check(sorting::Evaluate(1,inventory,30)==Rows({&stolen,&stolenGear,&unknown}),
          "Inventory stolen-only keeps flagged rows; unreadable status stays visible");
    sorting::g_pickers[0].category=1;
    check(sorting::Evaluate(1,inventory,30)==Rows({&stolen,&unknown}),"stolen-only is combined with the category");
    const auto reads=sorting::stolenReads;
    sorting::Evaluate(1,inventory,30);
    check(sorting::stolenReads==reads,"unchanged generation reuses row status");
    sorting::g_pickers[0].stolenOnly=false;
    check(sorting::Evaluate(1,inventory,30)==Rows({&legal,&stolen,&unknown}),"turning it off restores legal rows");
    sorting::g_pickers[0].category=0;
    UE::UObject p1{50,0,70,0},p2{51,0,71,1};
    sorting::g_pickers[2].stolenOnly=true;
    check(sorting::Evaluate(3,Rows{&p1,&p2},40)==Rows{&p2},"player container/barter side honours stolen-only");
    sorting::g_pickers[3].stolenOnly=true;
    UE::UObject o1{60,0,80,0},o2{61,0,81,1};
    check(sorting::Evaluate(4,Rows{&o1,&o2},40)==Rows({&o1,&o2}),"merchant/container side is never stolen-filtered");
    sorting::g_pickers[0].kind=1; sorting::g_pickers[0].stolenOnly=true;
    check(sorting::Evaluate(2,Rows{&legal,&stolen},41)==Rows({&legal,&stolen}),"Magic is never stolen-filtered");
    std::cout<<checks<<" checks, "<<failed<<" failures\n";
    return failed ? 1 : 0;
}
"""
cpp = out / "category_evaluator_tests.cpp"
cpp.write_text(prefix + extract("struct FilterState", True) + middle
               + extract("\tconst std::vector<UE::UObject*>& Evaluate(") + tests, encoding="utf-8")
exe = out / "category_evaluator_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp),
                *source_paths.include_flags(), f"/Fe:{exe}", f"/Fo:{out / 'category_evaluator_tests.obj'}"], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
