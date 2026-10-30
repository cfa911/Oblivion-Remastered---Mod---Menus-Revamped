"""Run the production Magic mask builder against spell/scroll snapshots."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
source = (root / 'src/sorting/ItemRows.cpp').read_text()
start = source.index('void ComputeMagicMasks(')
end = source.index('{', start) + 1
depth = 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
body = source[start:end]
prefix = r'''
#include "sorting/rules/MagicRows.h"
#include <vector>
#include <unordered_map>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
namespace UE { struct UObject { unsigned char data[0x60]{}; int index; }; }
using SpellRow = magicrows::Spell;
struct ScrollRow { int index = -1; SpellRow magic; };
constexpr auto kPassAll = magicrows::kPassAll;
struct FilterState { std::unordered_map<int, std::uint64_t> masks; };
namespace logger { template<class... T> void warn(const char*, T...) {} }
int Index(UE::UObject* p) { return p ? p->index : -1; }
bool ReadByte(const void* p, std::size_t off, std::uint8_t& v) {
    v = static_cast<const unsigned char*>(p)[off]; return true;
}
int ReadSpellsRaw(SpellRow* out, int) {
    out[0] = {{magiceffects::Code("FRDG")}, 1, 0, true};
    out[1] = {{magiceffects::Code("FIDG")}, 1, 2, true};
    out[2] = {{magiceffects::Code("CALM")}, 1, 3, true}; return 3;
}
int ReadScrollsRaw(ScrollRow* out, int) {
    // Exact menu index takes precedence even when it overlaps a raw spell index.
    out[0] = {1, {{magiceffects::Code("FIDG")}, 1, 0, true}};
    out[1] = {601, {{magiceffects::Code("REHE")}, 1, 0, true}};
    out[2] = {602, {}}; // unresolved enchantment must fail open
    return 3;
}
void Check(bool ok, const char* name) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", name); std::exit(1); }
}
'''
main = r'''
int main() {
    UE::UObject fire, restore, unknown, active;
    fire.index=1; restore.index=601; unknown.index=602; active.index=603;
    for (auto* row : {&fire, &restore, &unknown}) {
        row->data[0x51]=1; row->data[0x58]=2;
    }
    active.data[0x51]=8;
    FilterState state;
    ComputeMagicMasks({&fire,&restore,&unknown,&active},state);
    using magiceffects::Choice;
    const auto f=state.masks[1], r=state.masks[601];
    Check(!(f & kPassAll), "resolved scroll is classified");
    Check(magicrows::Passes(f,3,unsigned(Choice::Fire)), "scroll uses enchantment, not overlapping spell index");
    Check(!magicrows::Passes(f,3,unsigned(Choice::Frost)), "fire scroll excluded from Frost");
    Check(!magicrows::Passes(f,1,0), "scroll excluded from unrelated school");
    Check(!magicrows::Passes(f,7,0), "scroll is not a Power");
    Check(!(r & kPassAll), "scroll beyond spell snapshot limit resolves");
    Check(magicrows::Passes(r,3,unsigned(Choice::Restore)), "restore scroll classified");
    Check(!magicrows::Passes(r,3,unsigned(Choice::Fire)), "restore scroll excluded from Fire");
    Check((state.masks[602]&kPassAll)!=0, "unresolved scroll effects fail open");
    Check((state.masks[602]&magicrows::kScrollTag)!=0, "unresolved scroll retains known type");
    Check((f&magicrows::kScrollTag)!=0, "resolved scroll carries type membership");
    Check(state.masks[603]==kPassAll, "Active effects unchanged");
    UE::UObject greater,lesser; greater.index=2; lesser.index=3;
    greater.data[0x51]=lesser.data[0x51]=4;
    ComputeMagicMasks({&greater,&lesser},state);
    Check((state.masks[2]&magicrows::kPowerTag)!=0, "greater power has separate type");
    Check((state.masks[3]&magicrows::kPowerTag)!=0, "lesser power has separate type");
    Check(!magicrows::TypePasses(state.masks[2],1), "Spells excludes greater power");
    Check(!magicrows::TypePasses(state.masks[3],1), "Spells excludes lesser power");
    std::puts("16 Magic scroll/power checks passed");
}
'''
cpp = out / 'magic_scroll.cpp'
exe = out / 'magic_scroll.exe'
cpp.write_text(prefix + body + main)
subprocess.run(['cl','/nologo','/std:c++latest','/EHsc',f'/I{root / "src"}',
                str(cpp),f'/Fe{exe}',f'/Fo{out}\\'],check=True)
subprocess.run([str(exe)],check=True)

# Also exercise the real native reader with the observed node/payload layout.
reader_start = source.index('static void* ScrollHead(')
reader_end = source.index('void ComputeMagicMasks(', reader_start)
reader = source[reader_start:reader_end]
native_mocks = r'''
#define __try try
#define __except(x) catch (...)
namespace RE {
enum class FormType { Book, Enchantment };
struct TESBoundObject { FormType type; bool Is(FormType t) const { return type==t; } };
struct Effect { void* vtable=nullptr; std::uint32_t code=0; };
struct Effects { Effect* item=nullptr; Effects* next=nullptr; };
struct EnchantmentItem : TESBoundObject { Effects effectList; };
struct TESObjectBOOK : TESBoundObject { EnchantmentItem* formEnchanting=nullptr; };
struct ItemChange { void* extraData=nullptr; int count=1; TESBoundObject* object=nullptr; };
}
void* headAddress=nullptr;
namespace REL {
struct ID { explicit ID(unsigned value) { if(value!=1464952) std::abort(); } };
template<class T> struct Relocation {
    explicit Relocation(ID) {}
    auto& operator*() const { return *reinterpret_cast<T>(headAddress); }
};
}
'''
native_prefix = prefix[:prefix.index('int ReadScrollsRaw(')] + native_mocks + reader + prefix[prefix.index('void Check('):]
fixture = r'''
    RE::Effect fireEffect{nullptr,magiceffects::Code("FIDG")};
    RE::Effect restoreEffect{nullptr,magiceffects::Code("REHE")};
    RE::EnchantmentItem fireEnchant, restoreEnchant;
    fireEnchant.type=restoreEnchant.type=RE::FormType::Enchantment;
    fireEnchant.effectList.item=&fireEffect; restoreEnchant.effectList.item=&restoreEffect;
    RE::TESObjectBOOK fireBook, restoreBook, unreadBook;
    fireBook.type=restoreBook.type=unreadBook.type=RE::FormType::Book;
    fireBook.formEnchanting=&fireEnchant; restoreBook.formEnchanting=&restoreEnchant;
    RE::ItemChange items[3];
    items[0].object=&fireBook; items[1].object=&restoreBook; items[2].object=&unreadBook;
    struct Identity { RE::ItemChange* item; int index; } identities[]{{&items[0],1},{&items[1],601},{&items[2],602}};
    struct Node { Node* next=nullptr; Node* previous=nullptr; Identity* identity=nullptr; } nodes[3];
    for(int i=0;i<3;++i) { nodes[i].identity=&identities[i]; if(i<2) nodes[i].next=&nodes[i+1]; }
    auto* head=&nodes[0]; headAddress=&head;
    ScrollRow bounded[1];
    Check(ReadScrollsRaw(bounded,1)==1 && bounded[0].index==1, "native reader respects output bound");
'''
native_main = main.replace('int main() {', 'int main() {' + fixture)
native_cpp = out / 'magic_scroll_native.cpp'
native_exe = out / 'magic_scroll_native.exe'
native_cpp.write_text(native_prefix + body + native_main)
subprocess.run(['cl','/nologo','/std:c++latest','/EHsc',f'/I{root / "src"}',
                str(native_cpp),f'/Fe{native_exe}',f'/Fo{out}\\'],check=True)
subprocess.run([str(native_exe)],check=True)
