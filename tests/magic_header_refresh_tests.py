"""Production immediate-header refresh and collapse bodies: ownership, page and no-op guards."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
source=source_paths.read('picker')
prefix=r'''
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
namespace UE {
struct UClass {};
struct UObject { UClass* cls=nullptr; UObject* vm=nullptr; bool visible=true; int writes=0; UClass* GetClass(){return cls;} };
}
unsigned pageValue=0;
bool backgroundMasked=true;
namespace reflect {
using Handle=UE::UObject*; UE::UObject* Get(Handle h){return h;}
int Offset(UE::UClass*,const char*){return offsetof(UE::UObject,vm);}
template<class T>T* At(void* p,int offset){return p&&offset>=0?reinterpret_cast<T*>(static_cast<std::uint8_t*>(p)+offset):nullptr;}
}
namespace logger { template<class... T>void warn(T&&...){} }
namespace ui {
struct Call {
    UE::UObject* object;std::wstring name;std::uint8_t value=0;
    Call(UE::UObject* o,const wchar_t* n):object(o),name(n){}
    explicit operator bool() const {return object!=nullptr;}
    int Size(const char*){return 1;}
    template<class T>T Get(const char*){return static_cast<T>(name==L"IsVisible"?object->visible:pageValue);}
    bool Set(const char*,std::uint8_t v){value=v;return true;}
    bool Run(){if(!object)return false;if(name==L"SetVisibility"){object->visible=value!=1;++object->writes;}return true;}
};
}
namespace sorting {
namespace detail {
void UpdateNativeBackgroundMask(){backgroundMasked=false;}
struct Picker {
    int kind=-1;std::atomic<bool> active{false};reflect::Handle menu=nullptr,header=nullptr,nativeSchool=nullptr;
    void HideNativeReplacement();
    bool open=true;
    void Deactivate(){active=false;open=false;if(header)header->visible=false;}
} g_pickers[5];
'''
hide=source_paths.function(source,'\tvoid Picker::HideNativeReplacement(')
refresh=source_paths.function(source,'\tvoid RefreshMagicHeaders(')
tests=r'''
}
int main(){
using namespace sorting::detail;
unsigned checks=0,failed=0;
auto check=[&](bool ok,const char* why){++checks;if(!ok){++failed;std::cout<<"FAIL "<<why<<"\n";}};
UE::UObject vm,menu,other,school,type,effect,header,typeHeader;menu.vm=&vm;
auto& p=g_pickers[0];p.kind=1;p.active=true;p.menu=&menu;p.header=&header;p.nativeSchool=&school;
auto& t=g_pickers[4];t.kind=5;t.active=true;t.menu=&menu;t.header=&typeHeader;t.nativeSchool=&type;
auto& e=g_pickers[1];e.kind=2;e.active=true;e.menu=&menu;e.header=&header;e.nativeSchool=&effect;
sorting::RefreshMagicHeaders(&menu);
check(!school.visible&&!type.visible,"both native replacements collapse immediately");
check(effect.visible&&effect.writes==0,"unrelated native controls stay untouched");
auto writes=school.writes+type.writes;sorting::RefreshMagicHeaders(&menu);
check(school.writes+type.writes==writes,"already collapsed controls receive no setter calls");
school.visible=type.visible=true;sorting::RefreshMagicHeaders(&other);
check(school.visible&&type.visible,"a different Magic instance cannot change active headers");
pageValue=4;sorting::RefreshMagicHeaders(&menu);
check(school.visible&&type.visible,"Active Effects retains its native visibility rules");
check(!header.visible&&!typeHeader.visible&&!p.active.load()&&!t.active.load()&&!e.active.load(),
      "entering Active Effects hides and deactivates all custom Magic headers in the same native update");
check(!p.open&&!t.open&&!e.open,"entering Active Effects closes custom popups immediately");
check(!backgroundMasked,"native background mask is restored when Magic pickers deactivate");
pageValue=1;p.active=false;sorting::RefreshMagicHeaders(&menu);
check(school.visible&&type.visible,"inactive Magic leaves native controls alone");
p.active=true;t.header=nullptr;sorting::RefreshMagicHeaders(&menu);
check(!school.visible&&type.visible,"a native control is not hidden without its custom replacement");
school.visible=true;menu.vm=nullptr;sorting::RefreshMagicHeaders(&menu);
check(school.visible,"unreadable page fails open without hiding controls");
std::cout<<checks<<" checks, "<<failed<<" failures\n";return failed?1:0;
}
'''
cpp=out/'magic_header_refresh_tests.cpp'
cpp.write_text(prefix+hide+'\n} using namespace detail;\n'+refresh+tests,encoding='utf-8')
exe=out/'magic_header_refresh_tests.exe'
subprocess.run(['cl','/nologo','/EHsc','/std:c++latest',str(cpp),f'/Fe:{exe}',f'/Fo:{out / "magic_header_refresh_tests.obj"}'],check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
