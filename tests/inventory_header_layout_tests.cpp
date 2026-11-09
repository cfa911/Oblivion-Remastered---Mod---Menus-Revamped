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
// Icon centring (owner screenshot 2026-11-09): pre-fix offsets inside the 40-unit buttons.
#ifndef INVENTORYHEADERS_ICON_CENTER
namespace inventoryheaders { inline constexpr Box kSortIcon{6,3,32,32}, kCategoryIcon{4,1,36,36}; inline constexpr double kFilterLift=0; }
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
 Check(p.sorts[name].y==p.filters[2].y&&p.sorts[name].y-kFilterLift==p.filters[0].y&&p.sorts[name].y-kFilterLift==p.filters[1].y,"filters and sorts share one header row (toggles lifted to centre their icons)");
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
 const auto& lower=p.guides.back();
 const auto& upper=p.guides[p.guides.size()-2];
 Check(upper.x==lower.x&&upper.w==lower.w&&upper.h==lower.h&&upper.y==140,
  "upper divider matches the lower divider across the Inventory header");
 // RMI-069: vertical sort/filter separators use the same 2px gray rule as the horizontal header rules,
 // span the header strip from the upper rule to the lower rule, and the full-height list guides stay
 // subtle below the strip.
 {
  const auto p=Arrange(0,{100,140,800,600},190,columns,offered,3,{},700);
  const auto& lower=p.guides.back();const auto& upper=p.guides[p.guides.size()-2];
  const double listTop=140, stripBottom=listTop+kHeight+lower.h;
  int headerSeparators=0,listGuides=0;bool separatorShape=true,listBelowStrip=true,listHasHeaderPart=true;
  for(std::size_t i=0;i+2<p.guides.size();++i){
   const auto& g=p.guides[i];
   if(g.w<=0||g.h<=0)continue;
   if(g.y<stripBottom){++headerSeparators;
    if(!(g.w==upper.h&&g.y==listTop&&g.h==stripBottom-listTop))separatorShape=false;}
   else{++listGuides;
    bool match=false;
    for(const auto& h:p.guides)if(h.y==listTop&&h.h==stripBottom-listTop&&std::abs(h.x-g.x)<0.01)match=true;
    if(!match)listHasHeaderPart=false;}
   if(g.y<listTop+kHeight&&g.h>stripBottom-listTop)listBelowStrip=false;
  }
  Check(headerSeparators>=4,"header strip has sort and filter separators");
  Check(separatorShape,"vertical header separators are 2px rules spanning upper to lower rule");
  Check(listGuides==2&&listBelowStrip,"extended list guides start below the lower rule");
  Check(listHasHeaderPart,"each extended list guide continues as a rule inside the header strip");
  bool styled=false,styleStable=true;
  [&](const auto& P){
  if constexpr(requires{P.rule;}){
   const auto& p=P;
   styled=true;
   for(std::size_t i=0;i<p.guides.size();++i){
    const auto& g=p.guides[i];if(g.w<=0||g.h<=0)continue;
    if(p.rule[i]!=(g.y<stripBottom))styled=false;
   }
   auto fewer=offered;fewer[int(itemsort::Key::Health)]=false;fewer[int(itemsort::Key::Damage)]=false;
   auto q=P;q=Arrange(0,{100,140,800,600},190,columns,fewer,3,{},700);
   for(std::size_t i=0;i<q.guides.size();++i)
    if(q.guides[i].w>0&&p.guides[i].w>0&&q.rule[i]!=p.rule[i])styleStable=false;
  }}(p);
  Check(styled,"header separators use rule ink and list guides use the subtle ink");
  Check(styleStable,"each guide slot keeps one style across tabs");
 }
 for(int kind:{1,2,3,4,5,6})Check(!Arrange(kind,{100,140,800,600},190,columns,offered,3).enabled,"other menus retain existing layout");
 offered[health]=false;offered[damage]=false;
 p=Arrange(0,{100,140,800,600},190,columns,offered,1);
 Check(p.sorts[health].w==0&&p.sorts[damage].w==0,"hidden tab columns leave no header hit area");
 p=Arrange(0,{100,140,800,600},190,{},offered,3);
 Check(p.enabled&&p.sorts[value].w>0,"empty list has usable fallback sort buttons");
 Check(!Arrange(0,{100,140,0,0},190,columns,offered,3).enabled,"invalid geometry leaves native layout intact");
 {
  // The space between the upper and lower rules is centred at kRule+(kHeight-kRule)/2 = 21 below the strip top.
  const double centre=kRule+(kHeight-kRule)/2;
  Check(std::abs(kSortIcon.y+kSortIcon.h/2-centre)<0.01,"sort icons are centred between the rules");
  Check(std::abs(kCategoryIcon.y+kCategoryIcon.h/2-centre)<0.01,"category icon is centred between the rules");
  auto q=Arrange(0,{100,140,800,600},190,columns,offered,3);
  Check(kFilterLift>=2&&kFilterLift<=4&&std::abs(q.filters[0].y-(140-kFilterLift))<0.01&&std::abs(q.filters[1].y-(140-kFilterLift))<0.01,
   "stolen and enchanted toggles are lifted to the strip centre (measured 2.5 to 3.5 units low)");
 }
 std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
