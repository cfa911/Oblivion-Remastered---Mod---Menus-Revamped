#include <cmath>
#include <algorithm>
#include <cstdio>
#include <string_view>
#include <vector>
#include <memory>
#if __has_include("../src/engine/ScrollHintRules.h")
#include "../src/engine/ScrollHintRules.h"
#else
// Before relocation the native hint stays above the track and has no scoped placement policy.
namespace scrollhints::rules {
struct Box { double x,y,w,h; };
inline bool Target(std::string_view) { return false; }
inline Box Below(Box b) { return {b.x+b.w-38,b.y,36,36}; }
inline bool Show(bool,bool,bool,bool) { return false; }
}
#endif
struct Node { bool visible=true; bool stop=false; int id=0; std::vector<Node*> children; };
#ifndef SCROLL_HINT_VISIBLE_WALK
// T1 used TreeWidgets: every cached subtree consumed the same node budget before filtering.
template<class N,class Visible,class Children,class Visit>
void WalkVisible(N root,Visible visible,Children children,Visit visit,int& budget,int depth=0) {
 if(!root||depth>24||budget<=0) return;
 --budget;
 if(visible(root)) visit(root);
 for(auto child:children(root)) WalkVisible(child,visible,children,visit,budget,depth+1);
}
#else
using scrollhints::rules::WalkVisible;
#endif
int main() {
 int fails=0,checks=0;
 auto check=[&](bool ok,const char* label) { ++checks; if(!ok) { ++fails; std::printf("FAIL %s\n",label); } };
 using namespace scrollhints::rules;
 for(auto name:{"WBP_OriginalMenu_Inventory_C","WBP_ModernMenu_MagicMenu_C","WBP_ModernMenu_StatsContent_C","WBP_Modern_QuestPage_C","WBP_ModernMenu_Help_C"}) check(Target(name),"requested menu is included");
 for(auto name:{"WBP_ModernMenu_Container_C","WBP_ModernMenu_SpellPurchase_C","WBP_Modern_Settings_C","WBP_ModernMenu_AlchemyMenu_C"}) check(!Target(name),"unrequested menu remains untouched");
 for(Box list: {Box{168,351.25,737,550.75},Box{146.37,350.42,757.85,533.74},Box{181,271.5,730,595},Box{166,324.09,759,574.07},Box{175,263.6,738.7,630.4}}) {
  auto b=Below(list);
  check(std::abs(b.x+b.w*.5-(list.x+list.w-20))<.001,"hint aligns with the native track axis");
  check(std::abs(b.y+b.h*.5-(list.y+list.h+22))<.001,"hint sits below the list at the owner's marked footer position");
  check(b.w==36&&b.h==36,"native glyph proportions and size are preserved");
 }
 check(Show(true,true,true,true),"shown scrollable controller list displays its moved hint");
 check(!Show(false,true,true,true),"inactive tab does not display a hint");
 check(!Show(true,false,true,true),"keyboard and mouse input hide the moved hint");
 check(!Show(true,true,false,true),"a non-scrollable list does not get an extra hint");
 check(!Show(true,true,true,false),"missing glyph fails safely");
 // Reproduce cached tabs and large skill/list contents before the missed controls.
 std::vector<std::unique_ptr<Node>> pool;
 auto node=[&](int id=0) { pool.push_back(std::make_unique<Node>()); pool.back()->id=id; return pool.back().get(); };
 auto root=node(); auto inactive=node(); inactive->visible=false; root->children.push_back(inactive);
 for(int i=0;i<3500;++i) inactive->children.push_back(node());
 auto skills=node(); skills->stop=true; root->children.push_back(skills);
 for(int i=0;i<1100;++i) skills->children.push_back(node());
 for(int id:{1,2,3,4}) root->children.push_back(node(id)); // Details, Factions, Quests, Help
 int budget=1000; std::vector<int> found;
 WalkVisible(root,[](Node* n){return n->visible;},[](Node* n)->const auto&{return n->children;},[&](Node* n){if(n->id)found.push_back(n->id); return !n->stop;},budget);
 for(int id:{1,2,3,4}) check(std::find(found.begin(),found.end(),id)!=found.end(),"RMI-113 later Details/Factions/Quests/Help controls are discovered despite cached UI contents");
 check(budget>980,"RMI-113 inactive and row-content branches do not consume the discovery budget");
 std::printf("%d checks, %d failures\n",checks,fails); return fails?1:0;
}
