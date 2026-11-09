// RMI-068: a brush resource written in place keeps drawing the old material, because FSlateBrush caches its resource
// handle after the first paint (only SetResourceObject clears it). The handle sits after ImageType, 8-byte aligned.
#include <cstdio>
#if __has_include("../src/engine/BrushHandle.h")
#include "../src/engine/BrushHandle.h"
#else
// Pre-fix behavior: the handle is never located, so a swapped thumb material can stay pale.
namespace brushhandle {
constexpr int HandleOffset(int) { return -1; }
constexpr bool Fits(int, int) { return false; }
inline bool Plausible(const void*, const void*) { return false; }
}
#endif
unsigned checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
int main(){
 using namespace brushhandle;
 Check(HandleOffset(0xBF)==0xC0,"UE 5.3 layout: ImageType at 0xBF puts the handle at 0xC0");
 Check(HandleOffset(0x7B)==0x80&&HandleOffset(0x80)==0x88,"handle is the next 8-byte boundary after ImageType");
 Check(HandleOffset(-1)==-1,"missing ImageType gives no handle");
 Check(Fits(0xC0,0xD8),"handle and its flag byte fit a 0xD8 brush");
 Check(!Fits(0xC8,0xD8)&&!Fits(-1,0xD8),"a handle past the brush end is refused");
 int object=0,controller=0;
 Check(Plausible(nullptr,nullptr)&&Plausible(&object,&controller),"empty or full shared pointer is accepted");
 Check(!Plausible(&object,nullptr)&&!Plausible(nullptr,&controller),"a half-set pair is not a handle");
 std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
