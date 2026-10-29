"""Run the real reflected field lookups (reflect::Offset, ui::SizeOf) against struct doubles: a repeated lookup must not
walk the fields and convert every FName to a string again (315,000 lookups a second in the container menu,
2026-10-29), a name missing now is found once its field is linked, and a struct replaced at the same address is
read afresh."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
reflect_src = source_paths.read("reflect")
call_src = source_paths.read("ui-call")
reflect_bodies = "\n".join(source_paths.function(reflect_src, h) for h in (
    "\tnamespace\n\t{", "\tconst std::uint8_t* FindField(", "\tstd::int32_t Offset("))
call_bodies = "\n".join(source_paths.function(call_src, h) for h in ("\tnamespace\n\t{", "\tstd::int32_t SizeOf("))

prefix = r"""
#define NOMINMAX
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
int conversions = 0;
namespace UE {
struct FString { std::wstring s; };
inline const wchar_t* GetData(const FString& a) { return a.s.c_str(); }
inline int GetNum(const FString& a) { return int(a.s.size()) + 1; }
struct FName { const wchar_t* s; FString ToString() const { ++conversions; return { s }; } };
struct UFunction;
struct UObject { void ProcessEvent(UFunction*, void*) {} };
struct UStruct : UObject { UStruct* superStruct = nullptr; void* childProperties = nullptr; std::int32_t propertiesSize = 0, internalIndex = 7; };
}
namespace pe {
std::string Utf8(const UE::FString& a) { std::string o; for (wchar_t c : a.s) o.push_back(char(c)); return o; }
}
struct alignas(8) Field { std::uint8_t b[0x50]{}; };
Field* MakeField(const wchar_t* name, std::int32_t offset, std::int32_t size, Field* next) {
    auto* f = new Field;
    *reinterpret_cast<Field**>(f->b + 0x18) = next;
    *reinterpret_cast<UE::FName*>(f->b + 0x20) = UE::FName{ name };
    *reinterpret_cast<std::int32_t*>(f->b + 0x34) = size;
    *reinterpret_cast<std::int32_t*>(f->b + 0x44) = offset;
    return f;
}
"""

tests = r"""
int main() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::cout << "FAIL: " << why << "\n"; } };
    UE::UStruct base, item, other;
    base.childProperties = MakeField(L"c", 0, 4, nullptr);
    item.superStruct = &base;
    item.childProperties = MakeField(L"a", 8, 4, MakeField(L"b", 16, 8, nullptr));
    other.childProperties = MakeField(L"b", 40, 2, nullptr);
    other.internalIndex = 8;
    check(reflect::Offset(&item, "b") == 16 && reflect::Offset(&item, "c") == 0, "fields of the struct and its parent");
    conversions = 0;
    for (int i = 0; i < 1000; ++i) { reflect::Offset(&item, "b"); reflect::Offset(&item, "c"); }
    check(conversions == 0, "a repeated lookup reads no field names");
    check(ui::SizeOf(&item, "b") == 8 && ui::SizeOf(&item, "a") == 4, "element sizes");
    conversions = 0;
    for (int i = 0; i < 1000; ++i) ui::SizeOf(&item, "b");
    check(conversions == 0, "a repeated size lookup reads no field names");
    check(reflect::Offset(&other, "b") == 40 && ui::SizeOf(&other, "b") == 2, "same name, other struct");
    check(reflect::Offset(&item, "late") == -1, "a missing field");
    item.childProperties = MakeField(L"late", 32, 4, static_cast<Field*>(item.childProperties));
    check(reflect::Offset(&item, "late") == 32, "a field linked after a miss is found (misses are never kept)");
    item.childProperties = MakeField(L"b", 24, 4, nullptr);   // unloaded, another struct loaded at the same address
    check(reflect::Offset(&item, "b") == 24 && ui::SizeOf(&item, "b") == 4, "a struct replaced at its address is read afresh");
    item.internalIndex = 9;
    check(reflect::Offset(&item, "b") == 24, "a struct in another object slot is read afresh");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
"""
cpp = out / "field_cache_tests.cpp"
cpp.write_text(prefix + "namespace reflect {\n" + reflect_bodies + "\n}\nnamespace ui {\n" + call_bodies + "\n}\n" + tests,
               encoding="utf-8")
exe = out / "field_cache_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp), *source_paths.include_flags(),
                f"/Fe:{exe}", f"/Fo:{out / 'field_cache_tests.obj'}"], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
