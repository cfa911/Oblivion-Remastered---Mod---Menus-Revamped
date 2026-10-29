"""Run the real ProcessEvent detour and ui::Call bodies against object doubles: our own reflected calls must not reach
the watchers (they re-entered the list filter ~38,000 times a second in the container menu, 2026-10-29), while native
calls - including the ones the engine makes from inside our call - and every interceptor still do."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
hook = "\n".join(l for l in source_paths.read("pe-hook").splitlines() if not l.startswith("#include"))
call = source_paths.read("ui-call")
call_bodies = "\n".join(source_paths.function(call, head) for head in (
    "\tCall::Call(", "\tvoid* Call::At(", "\tbool Call::Run(", "\tbool GuardedProcessEvent(", "\tbool Call::RunGuarded(",
    "\tbool CallFirst("))
for marker in source_paths.UI_CALL_EXTRA:
    call_bodies = source_paths.function(call, marker) + "\n" + call_bodies

prefix = r"""
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
namespace UE {
struct FString { std::wstring s; };
inline const wchar_t* GetData(const FString& a) { return a.s.c_str(); }
inline int GetNum(const FString& a) { return int(a.s.size()) + 1; }
enum class EFindName { Find };
struct FName { std::wstring s; FName(const wchar_t* a, EFindName) : s(a) {} FString ToString() const { return { s }; } };
struct UStruct { UStruct* superStruct = nullptr; void* childProperties = nullptr; std::int32_t propertiesSize = 16; };
struct UFunction : UStruct { std::wstring name; FName GetFName() const { return FName(name.c_str(), EFindName::Find); } };
struct UClass;
struct UObject {
    void** vtable; UClass* cls = nullptr; std::vector<UFunction*> fns;
    UClass* GetClass() { return cls; }
    UFunction* FindFunction(const FName& n) { for (auto* f : fns) if (f->name == n.s) return f; return nullptr; }
    void ProcessEvent(UFunction* f, void* p) { reinterpret_cast<void (*)(UObject*, UFunction*, void*)>(vtable[0x4D])(this, f, p); }
};
struct UClass : UObject { UObject* cdo = nullptr; UObject* GetDefaultObject(bool) { return cdo; } FString GetFullName() { return { L"Test" }; } };
}
namespace logger { template<class... T> void info(T&&...) {} template<class... T> void warn(T&&...) {} template<class... T> void error(T&&...) {} }
namespace reflect { std::int32_t Offset(UE::UStruct*, std::string_view) { return 0; } }
#include "engine/PEHook.h"
#include "engine/Ui.h"
"""

tests = r"""
void* table[0x4E]{};
UE::UClass cls;
UE::UObject cdo, widget;
UE::UFunction isVisible, deactivated, focus;
int bodies = 0, watched = 0, watchedNested = 0, intercepted = 0;
bool nest = false;
void Original(UE::UObject* o, UE::UFunction* f, void* p) {
    ++bodies;
    if (f == &isVisible && nest) o->ProcessEvent(&deactivated, nullptr);   // the engine calls back from inside our call
}
void Post(UE::UObject*, UE::UFunction* f, void*) { f == &deactivated ? ++watchedNested : ++watched; }
bool Intercept(UE::UObject*, UE::UFunction* f, void*) { if (f == &focus) ++intercepted; return false; }
int main() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::cout << "FAIL: " << why << "\n"; } };
    alignas(8) static std::uint8_t firstParam[0x60]{};   // FProperty: offset 0 at +0x44, no next field
    focus.childProperties = firstParam;
    isVisible.name = L"IsVisible"; deactivated.name = L"BP_OnDeactivated"; focus.name = L"OnFocus";
    table[0x4D] = reinterpret_cast<void*>(&Original);
    cdo.vtable = widget.vtable = table; cdo.cls = widget.cls = &cls; cls.cdo = &cdo;
    widget.fns = { &isVisible, &focus };
    check(pe::WatchPost(&cls, &Post) && pe::Intercept(&cls, &Intercept), "watchers registered");
    int marker = 1;
    widget.ProcessEvent(&isVisible, nullptr);
    check(watched == 1, "a native call runs its watchers");
    watched = 0;
    for (int i = 0; i < 100; ++i) { ui::Call vis(&widget, L"IsVisible"); vis.Run(); }
    check(bodies == 101, "our own calls still run the function body");
    check(watched == 0, "our own calls never run the watchers (no self-triggered filtering)");
    nest = true; { ui::Call vis(&widget, L"IsVisible"); vis.Run(); } nest = false;
    check(watchedNested == 1, "a native event fired from inside our call is still watched");
    check(watched == 0, "only our exact call is skipped");
    ui::CallFirst(&widget, L"OnFocus", &marker, sizeof(marker));
    check(intercepted == 1, "interceptors still see our own calls");
    check(watched == 0, "CallFirst is an own call too");
    UE::UFunction* fn = &isVisible;
    ui::GuardedProcessEvent(&widget, fn, nullptr);
    check(watched == 0, "a guarded call is an own call too");
    widget.ProcessEvent(&isVisible, nullptr);
    check(watched == 1, "the next native call is watched again");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
"""
cpp = out / "own_call_tests.cpp"
cpp.write_text(prefix + hook + "\nnamespace ui {\n" + call_bodies + "\n}\n" + tests, encoding="utf-8")
exe = out / "own_call_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp), *source_paths.include_flags(),
                f"/Fe:{exe}", f"/Fo:{out / 'own_call_tests.obj'}"], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
