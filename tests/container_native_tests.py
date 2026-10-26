"""Run the real TES Process body with captured null-form row fixtures and native doubles."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
source = (root / "sortingoverhaul/GearData.cpp").read_text()

def extract(marker):
    start = source.index(marker)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

cpp = out / "container_native_tests.cpp"
cpp.write_text(r"""
#include <array>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>
namespace RE {
struct TESBoundObject {};
struct TESObjectREFR {};
struct ItemChange { TESBoundObject* object = nullptr; };
struct PlayerCharacter : TESObjectREFR { static PlayerCharacter* GetSingleton(); };
struct TESForm { template<class T> static T* LookupByID(std::uint32_t) { return nullptr; } };
}
RE::PlayerCharacter player;
RE::TESObjectREFR merchant;
RE::TESBoundObject object;
RE::ItemChange item{&object};
RE::PlayerCharacter* RE::PlayerCharacter::GetSingleton() { return &player; }
struct Call { RE::TESObjectREFR* owner; int index; bool barter; };
std::vector<Call> calls;
RE::ItemChange* Resolve(RE::TESObjectREFR* owner, int index, bool barter) {
    calls.push_back({owner,index,barter});
    return ((owner == &player && index == 35 && !barter) ||
        (owner == &merchant && index == 19 && barter)) ? &item : nullptr;
}
using Resolver = RE::ItemChange* (*)(RE::TESObjectREFR*, int, bool);
namespace REL {
struct ID { explicit ID(unsigned) {} };
template<class F> struct Relocation {
    explicit Relocation(ID) {}
    template<class... A> auto operator()(A... args) {
        if constexpr (std::is_same_v<F, Resolver>) return Resolve(args...);
    }
};
}
namespace logger { template<class... A> void warn(A&&...) {} }
std::uint64_t GetTickCount64() { return 100; }
bool Read(const void* source, void* dest, std::size_t size) {
    if (!source) return false;
    std::memcpy(dest,source,size); return true;
}
bool Tags(RE::ItemChange* copy, std::uint32_t& tags) {
    tags = 1; return copy && copy->object;
}
struct Entry { int index; std::uint32_t tags; bool valid; };
std::mutex g_lock;
std::atomic<std::uint64_t> g_epoch[3]{};
std::atomic<bool> g_pending[3]{};
""" + extract("\t\tstruct Work") + r""";
std::unique_ptr<Work> g_queued[3], g_ready[3], g_work[3];
""" + extract("\t\tvoid Process(") + r"""
template<class F> void Invoke(F fn, unsigned slot, void* menu) {
    if constexpr (std::is_invocable_v<F,unsigned,void*>) fn(slot,menu);
    else fn(slot);
}
template<class W> void Fill(W& work, int index, bool side) {
    work.indices = {side ? 0 : index};  // local mask key must not replace the native index
    if constexpr (requires { work.nativeIndices; }) work.nativeIndices = {index};
}
int main() {
    alignas(8) std::array<std::uint8_t,0xB0> menu{};
    RE::TESObjectREFR* target = &merchant;
    std::memcpy(menu.data()+0x88,&target,sizeof(target));
    menu[0xA9] = 1;
    for (unsigned slot : {1u,2u,0u}) {
        auto work = std::make_unique<Work>();
        work->ticket = 4+slot; work->deadline = 2000; work->player = &player;
        Fill(*work,slot==2?19:35,slot!=0); work->forms = {0};
        g_epoch[slot] = work->ticket; g_pending[slot] = true;
        g_queued[slot] = std::move(work);
        Invoke(&Process,slot,menu.data());
        if (!g_ready[slot] || g_ready[slot]->entries.size()!=1 ||
            !g_ready[slot]->entries[0].valid) {
            std::cerr << "FAIL: captured null-form row unresolved in slot " << slot << "\n";
            return 1;
        }
    }
    if (calls.size()!=3 || calls[0].owner!=&player || calls[0].index!=35 || calls[0].barter ||
        calls[1].owner!=&merchant || calls[1].index!=19 || !calls[1].barter ||
        calls[2].owner!=&player || calls[2].index!=35 || calls[2].barter) return 2;
    std::cout << "PASS: null-form player/merchant rows use exact side/index/barter; Inventory unchanged\n";
}
""")
exe = out / "container_native_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp),
                f"/Fe:{exe}", f"/Fo:{out / 'container_native_tests.obj'}"], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
