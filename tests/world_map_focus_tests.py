"""Exercise the real focus adapter: revalidation, exact identity, sole native operation."""
from pathlib import Path
import subprocess
import sys
import source_paths
root=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
body=source_paths.function((root/'src/engine/WorldMap.cpp').read_text(),'bool Focus(')
prefix=r'''
#define NOMINMAX
#include <Windows.h>
#include <iostream>
#include "search/WorldMapRules.h"
namespace UE { struct UObject { float zoom=2; }; struct FVector2d { double x,y; }; }
std::vector<maprules::Location> snapshot;
UE::UObject object; bool live=true, available=true, sizeOK=true, succeeds=true;
int calls=0; std::wstring operation; UE::FVector2d written{0,0};
auto Snapshot() { return snapshot; }
UE::UObject* Model() { return live?&object:nullptr; }
namespace ui {
struct Call {
 Call(UE::UObject*,const wchar_t* name) { operation=name; }
 explicit operator bool() const { return available; }
 int Size(const char*) { return sizeOK?16:8; }
 bool Set(const char* name,const UE::FVector2d& value) { if(std::string(name)!="NewMapPosition")return false;written=value;return true; }
 bool RunGuarded() { ++calls;return succeeds; }
};
}
'''
main=r'''
int main() {
 int checks=0,failed=0;
 auto check=[&](bool b,const char* why){++checks;if(!b){++failed;std::cerr<<"FAIL "<<why<<'\n';}};
 snapshot={{"a","shared","a","Same Name",10,20,3,true},{"b","shared","b","Same Name",30,40,3,true},{"hidden","h","h","Hidden",0,0,3,false}};
 check(Focus("b") && calls==1 && written.x==30 && written.y==40,"selected duplicate resolves exact fresh marker");
 check(operation==L"ForceSetMapPosition" && object.zoom==2,"sole operation is focus and adapter preserves zoom");
 calls=0; check(!Focus("removed") && !calls,"stale key produces no call");
 check(!Focus("hidden") && !calls,"discovery rechecked before action");
 snapshot[0].passesNativeFilter=false;check(!Focus("a") && !calls,"selection rejects newly disabled native filter");snapshot[0].passesNativeFilter=true;
 live=false;check(!Focus("a") && !calls,"missing live model produces no call");live=true;
 sizeOK=false;check(!Focus("a") && !calls,"incompatible parameter layout rejected");sizeOK=true;
 succeeds=false;check(!Focus("a"),"failed native call reported");
 std::cout<<checks<<" checks, "<<failed<<" failures\n";return failed?1:0;
}
'''
cpp=out/'map_focus.cpp';exe=out/'map_focus.exe';cpp.write_text(prefix+body+main)
subprocess.run(['cl','/nologo','/EHsc','/std:c++20','/utf-8','/I'+str(root/'src'),str(cpp),'/Fo'+str(out/'focus.obj'),'/Fe'+str(exe)],check=True)
subprocess.run([str(exe)],check=True)
