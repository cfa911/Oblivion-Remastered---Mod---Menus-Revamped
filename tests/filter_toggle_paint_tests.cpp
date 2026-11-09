// Header stolen/enchanted toggles. Inventory dark skin (owner, 2026-11-09): inverted. On = the icon in the row marker's
// colour (red hand, blue orb) with no coloured fill; off = dimmed light icon. Other menus keep the coloured-fill look.
#include <cstdio>
#if __has_include("../src/sorting/rules/FilterTogglePaint.h")
#include "../src/sorting/rules/FilterTogglePaint.h"
#else
// Pre-feature behavior: every menu uses the coloured fill with a light icon.
namespace filtertoggle {
inline constexpr float kClear[4]{0,0,0,0}, kLight[4]{1,1,1,1};
struct Paint{const float* fill;const float* icon;float opacity;};
inline Paint Choose(bool,bool on,bool focus,const float* focusFill,const float* onFill,const float*,float dim)
{return {focus?focusFill:on?onFill:kClear,kLight,on?1.0f:dim};}
}
#endif
unsigned checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
int main(){
 using namespace filtertoggle;
 const float focus[4]{0.3f,0.3f,0.3f,1},fill[4]{0.7f,0.13f,0.11f,0.5f},mark[4]{0.7f,0.13f,0.11f,1};
 auto p=Choose(true,true,false,focus,fill,mark,0.5f);
 Check(p.fill[3]==0,"Inventory: an active toggle has no coloured fill");
 Check(p.icon==mark&&p.opacity==1,"Inventory: an active toggle shows the icon in the row marker colour");
 p=Choose(true,false,false,focus,fill,mark,0.5f);
 Check(p.fill[3]==0&&p.icon[0]==1&&p.icon[1]==1&&p.opacity==0.5f,"Inventory: an inactive toggle is a dimmed light icon");
 p=Choose(true,true,true,focus,fill,mark,0.5f);
 Check(p.fill==focus&&p.icon==mark,"Inventory: controller focus keeps the focus fill and the coloured icon");
 p=Choose(false,true,false,focus,fill,mark,0.5f);
 Check(p.fill==fill&&p.icon[0]==1&&p.icon[2]==1,"other menus keep the coloured fill with a light icon");
 std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
