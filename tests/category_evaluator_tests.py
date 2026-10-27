"""Actual category evaluator body: generations, native side indices and pending results."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
source = (root / "sortingoverhaul/GearFilter.cpp").read_text(encoding="utf-8")


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
namespace UE { struct UObject { int index=0; std::uint32_t form=0; std::uint64_t key=0; }; }
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
namespace gearfilter {
"""
middle = r"""
FilterState g_states[3];
std::unordered_map<std::uint64_t,std::uint64_t> g_rowTags;
struct Picker {
    int kind=0; unsigned choice=0,appliedChoice=0,category=0,appliedCategory=0;
    bool Passes(std::uint64_t mask) const { return !category || (mask & category)!=0; }
} g_pickers[4];
bool active=true;
bool Active(int) { return active; }
int Index(UE::UObject* item) { return item->index; }
std::uint32_t ContainerForm(UE::UObject* item,std::uint64_t* key=nullptr) {
    if(key) *key=item->key ? item->key : item->form; return item->form;
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
    gearfilter::g_pickers[2].category=1;
    check(gearfilter::Evaluate(3,source,1)==source,"pending classification fails open using native rows");
    check(geardata::requests.back().slot==1 &&
          geardata::requests.back().indices==std::vector<int>({0,1}) &&
          geardata::requests.back().native==std::vector<int>({7,9}),
          "player request preserves source positions and exact native indices");
    geardata::replies[1]={{0,1,true},{1,2,true}};
    check(gearfilter::Evaluate(3,source,1)==Rows{&first},"ready tags filter player category");
    gearfilter::Evaluate(3,source,1);
    check(geardata::requests.size()==1,"same generation never requeues consumed classification");
    geardata::replies[1]={{0,1,true},{1,2,true}}; // stale old generation
    first.form=3; second.form=4; // reused wrappers now hold other items
    check(gearfilter::Evaluate(3,source,2)==source,"reused wrappers start new generation rather than old tag projection");
    check(gearfilter::g_states[1].ticket==2,"new generation owns a different classification ticket");
    geardata::replies[2]={{0,2,true},{1,1,true}};
    check(gearfilter::Evaluate(3,source,2)==Rows{&second},"only current generation reply affects category");
    gearfilter::g_pickers[3].category=2;
    UE::UObject m1{7,60},m2{9,61};
    Rows merchant{&m1,&m2};
    gearfilter::Evaluate(4,merchant,1);
    check(geardata::requests.back().slot==2,"merchant has its own request slot");
    geardata::replies[3]={{0,2,true},{1,0,false}};
    check(gearfilter::Evaluate(4,merchant,1)==merchant,"unresolved merchant rows fail open");
    check(gearfilter::Evaluate(3,source,2)==Rows{&second},"merchant state cannot replace player tags");
    gearfilter::Evaluate(4,merchant,2);
    now+=3000;
    check(gearfilter::Evaluate(4,merchant,2)==merchant && !gearfilter::g_states[2].ticket,
          "timeout cancels pending request and leaves native rows usable");
    const auto count=geardata::requests.size();
    gearfilter::Evaluate(4,merchant,2);
    check(geardata::requests.size()==count,"timed-out generation cannot requeue forever");
    // RMI-005: a rebuild after a sale/deposit must not show rows already known to fail the category.
    // Container/barter rows have null forms (docs/inventory/internals.md section 4): only the row key identifies them.
    UE::UObject keep{20,0,50},drop{21,0,51},fresh{22,0,52};
    gearfilter::Evaluate(3,Rows{&keep,&drop},10);
    geardata::replies[geardata::next]={{0,1,true},{1,2,true}};
    check(gearfilter::Evaluate(3,Rows{&keep,&drop},10)==Rows{&keep},"classified player rows filter");
    check(gearfilter::Evaluate(3,Rows{&drop,&keep},11)==Rows{&keep},
          "rebuild keeps known rows filtered while the new request is pending");
    check(gearfilter::Evaluate(3,Rows{&drop,&keep,&fresh},12)==Rows{&keep,&fresh},
          "rows never classified stay visible while pending");
    UE::UObject sold{30,0,51};
    check(gearfilter::Evaluate(4,Rows{&sold},20)==Rows{&sold},"merchant category 2 keeps the sold row");
    gearfilter::g_pickers[3].category=1;
    check(gearfilter::Evaluate(4,Rows{&sold},21).empty(),"row moved to the other side uses its known tags at once");
    geardata::replies[geardata::next]={{0,1,true}};
    check(gearfilter::Evaluate(4,Rows{&sold},21)==Rows{&sold},"fresh reply replaces carried tags");
    gearfilter::g_pickers[3].category=2;
    const auto magicCount=geardata::requests.size();
    gearfilter::g_pickers[0].kind=1; gearfilter::g_pickers[0].category=1;
    check(gearfilter::Evaluate(2,source,1)==Rows{&first},"Magic category tags remain supported");
    check(geardata::requests.size()==magicCount,"Magic does not use inventory/container request path");
    std::cout<<checks<<" checks, "<<failed<<" failures\n";
    return failed ? 1 : 0;
}
"""
cpp = out / "category_evaluator_tests.cpp"
cpp.write_text(prefix + extract("struct FilterState", True) + middle
               + extract("\tconst std::vector<UE::UObject*>& Evaluate(") + tests, encoding="utf-8")
exe = out / "category_evaluator_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp),
                f"/Fe:{exe}", f"/Fo:{out / 'category_evaluator_tests.obj'}"], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
