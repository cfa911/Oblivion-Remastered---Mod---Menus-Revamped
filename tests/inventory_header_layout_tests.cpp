// Geometry acceptance: native baseline has one right-aligned row; the new Inventory row is separate.
#include <array>
#include <cmath>
#include <cstdio>
#include "../src/sorting/rules/ItemSort.h"
#if __has_include("../src/sorting/rules/InventoryHeaderLayout.h")
#include "../src/sorting/rules/InventoryHeaderLayout.h"
#else
// Pre-feature behavior: native carousel rectangles are unchanged, with filters and sorts on one row.
namespace inventoryheaders {
struct Box { double x=0,y=0,w=0,h=0; };
constexpr int kCount=static_cast<int>(itemsort::Key::kCount);
struct Plan { bool enabled=false; double inset=0; std::array<Box,kCount> sorts{}; std::array<Box,3> filters{}; };
inline Plan Arrange(int kind,Box list,double,const std::array<Box,kCount>& columns,const std::array<bool,kCount>& offered,int count){
 Plan out;out.enabled=kind==0;
 for(int i=0;i<kCount;++i)if(offered[i])out.sorts[i]={list.x+list.w-300+i*49,list.y-49,49,49};
 for(int i=0;i<count;++i)out.filters[i]={list.x+300+i*30,list.y-49,30,49};return out;
}
}
#endif
unsigned checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
int main(){
 using namespace inventoryheaders;
 const int name=int(itemsort::Key::Name),health=int(itemsort::Key::Health),damage=int(itemsort::Key::Damage),value=int(itemsort::Key::Value);
 std::array<Box,kCount> columns{};std::array<bool,kCount> offered{};
 for(auto key:itemsort::Keys(itemtags::Tab::Weapons))offered[int(key)]=true;
 columns[health]={600,150,54,28};columns[damage]={654,150,54,28};
 columns[int(itemsort::Key::Weight)]={708,150,54,28};columns[value]={762,150,87,28};
 auto p=Arrange(0,{100,140,800,600},190,columns,offered,3);
 Check(p.enabled,"Inventory layout enabled");
 Check(p.inset>=40&&p.inset<=49,"dedicated header strip reserved without moving the list bottom");
 Check(p.sorts[name].x==190,"A-Z aligns with the item name rather than the numeric sorts");
 Check(p.sorts[name].y==p.filters[2].y&&p.sorts[name].y==p.filters[0].y&&p.sorts[name].y==p.filters[1].y,"filters and sorts share one header row");
 Check(std::abs(p.sorts[health].x+p.sorts[health].w/2-(600+27))<0.01,"condition icon centered on measured condition column");
 Check(std::abs(p.sorts[value].x+p.sorts[value].w/2-(762+43.5))<0.01,"value icon centered on measured value column");
 Check(p.sorts[health].h<=40,"compact icon row");
 Check(p.filters[2].x+p.filters[2].w<=p.sorts[name].x,"category sits over the item icons before A-Z");
 Check(p.filters[1].x+p.filters[1].w<=p.filters[0].x&&p.filters[0].x+p.filters[0].w/2<columns[health].x,"hand and orb centers precede the first numeric column");
 Check(std::abs(p.filters[0].x+p.filters[0].w/2-(columns[health].x-17))<0.01,"header orb retains its original position");
 bool sideBorder=false;
 for (const auto& guide:p.guides)
  if (guide.h>1 && guide.w==1 && (guide.x==100 || guide.x==878)) sideBorder=true;
 Check(!sideBorder,"header and rows have no outer vertical borders");
 for(int kind:{1,2,3,4,5,6})Check(!Arrange(kind,{100,140,800,600},190,columns,offered,3).enabled,"other menus retain existing layout");
 offered[health]=false;offered[damage]=false;
 p=Arrange(0,{100,140,800,600},190,columns,offered,1);
 Check(p.sorts[health].w==0&&p.sorts[damage].w==0,"hidden tab columns leave no header hit area");
 p=Arrange(0,{100,140,800,600},190,{},offered,3);
 Check(p.enabled&&p.sorts[value].w>0,"empty list has usable fallback sort buttons");
 Check(!Arrange(0,{100,140,0,0},190,columns,offered,3).enabled,"invalid geometry leaves native layout intact");
 std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
