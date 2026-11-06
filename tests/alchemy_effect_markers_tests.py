"""Execute the production ingredient decorator against recycled-row/UI doubles."""
from pathlib import Path
import subprocess
import sys
import source_paths

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
src = source_paths.read('alchemy')
prefix = r'''
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "sorting/rules/AlchemyRules.h"
#if __has_include("sorting/rules/AlchemyEffectMarkers.h")
#include "sorting/rules/AlchemyEffectMarkers.h"
#endif
using namespace alchemyrules;
namespace UE { struct UObject { std::wstring id; std::string text; UObject* parent=nullptr; bool visible=false, circle=false; double x=0,y=0,w=0,h=0,ax=0; float line=0; std::array<float,4> fill{}, outline{}; }; }
std::map<std::wstring, UE::UObject> widgets;
UE::UObject text, canvas, weight, price;
namespace reflect {
using Handle = UE::UObject*;
Handle Hold(UE::UObject* object) { return object; }
UE::UObject* Get(Handle object) { return object; }
}
int markerBrushWrites = 0;
namespace ui {
UE::UObject* FindInTree(UE::UObject*, const char* name) {
if(std::string(name)=="CanvasPanel") return &canvas;
if(std::string(name)=="MrAlchemyWeight") return &weight;
if(std::string(name)=="MrAlchemyValue") return &price;
std::wstring id(name,name+std::strlen(name)); auto it=widgets.find(id); return it==widgets.end()?nullptr:&it->second;
}
bool IsWithin(UE::UObject* o,UE::UObject* p) {for(;o;o=o->parent)if(o==p)return true;return false;}
UE::UObject* AddToCanvas(UE::UObject* p,const wchar_t*,const wchar_t* name,UE::UObject** slot) { auto& o=widgets[name]; o.id=name; o.parent=p; *slot=&o; return &o; }
void Visible(UE::UObject* o,bool v) { if(o)o->visible=v; }
void Anchors(UE::UObject* o,double x,double,double,double) {o->ax=x;}
void Vec2(UE::UObject* o,const wchar_t* action,double x,double y) { if(std::wstring(action)==L"SetPosition"){o->x=x;o->y=y;} if(std::wstring(action)==L"SetSize"){o->w=x;o->h=y;} }
bool CallFirst(UE::UObject*,const wchar_t*,const void*,size_t) {return true;}
double ViewportScale(){return 1;}
bool RoundedBox(UE::UObject* o,bool circle,const float* fill,const float* outline,float line,float=6) {++markerBrushWrites; o->circle=circle; o->line=line; std::copy_n(fill,4,o->fill.begin());std::copy_n(outline,4,o->outline.begin());return true;}
bool Measure(UE::UObject*,double&,double&,double&,double&){return false;}
}
namespace uix {std::string GetText(UE::UObject* o){return o->text;} void SetText(UE::UObject* o,const std::string& t){o->text=t;}}
UE::UObject* SlotOf(UE::UObject* o){return o;}
UE::UObject* RowNameText(UE::UObject*){return &text;}
UE::UObject* ColumnLabel(UE::UObject*,UE::UObject*,const wchar_t*,double){return nullptr;}
double g_columnDy=0; bool g_dyKnown=true;
std::unordered_set<UE::UObject*> g_dyDone;
constexpr double kWeightCx=120,kValueCx=45,kIconStep=52;
std::unordered_map<std::string,std::pair<float,int>> g_formData,g_formMatch;
std::unordered_map<std::string,std::vector<std::string>> g_markerEffects;
std::vector<std::string> g_markerNames;
'''
main = r'''
int main(){
int checks=0,failed=0;
auto check=[&](bool ok,const char* why){++checks;if(!ok){++failed;std::cerr<<"FAIL "<<why<<"\n";}};
auto visible=[](){int n=0;for(auto& [id,w]:widgets) n+=w.visible;return n;};
UE::UObject row; canvas.parent=&row; text.text="[Ingr] Test Herb";
g_markerNames={"Fire Shield","Chameleon","Invisibility","Light","Nighteye","Dispel","Reflect Damage","Reflect Spell","Cure Disease"};
g_markerEffects[MatchKey(text.text)]={"Light","Fire Shield","Chameleon","Invisibility"};
DecorateIngredientRow(&row);
check(visible()==4,"four known selected effects draw four markers");
check(weight.x == -114.25 && price.x == -45, "Weight text is centered between its dividers; Value keeps its center");
bool circles=true,aligned=true,stroke=true;
double right=-1e6,left=1e6;
for(auto& [id,w]:widgets) if(w.visible){circles &= w.circle;aligned &= w.ax==1 && w.x+w.w<=-kWeightCx-10;stroke &= w.line==1;right=std::max(right,w.x+w.w);left=std::min(left,w.x);}
check(visible()>0 && circles,"markers are circles, not squares");
check(visible()>0 && aligned,"group is anchored before Weight, not after variable-length names");
check(visible()>0 && stroke,"one-pixel outline");
#if __has_include("sorting/rules/AlchemyEffectMarkers.h")
const auto outline=MarkerOutline();
check(std::abs(outline[0]-MarkerLinear(76.f/255))<0.00001 && std::abs(outline[1]-MarkerLinear(55.f/255))<0.00001 && std::abs(outline[2]-MarkerLinear(47.f/255))<0.00001,"outline is sRGB #4c372f converted to linear");
check(IngredientMarkers({"[Alte] Fire Shield","Light"},{"Light","Fire Shield"})==std::vector<int>{0,1},"marker order follows legend, prefixes normalize");
check(IngredientMarkers({"Light","Light"},{"Light","Light"})==std::vector<int>{0},"duplicate known/selected names never duplicate circles");
check(IngredientMarkers({"a","b","c","d","e"},{"a","b","c","d","e"}).size()==4,"four-circle cap even with malformed extra known effects");
check(IngredientMarkers({"unknown"},{"Light"}).empty(),"effects outside known selected intersection are absent");
bool distinct=true;for(int i=0;i<8;++i)for(int j=i+1;j<8;++j)distinct &= MarkerColor(i)!=MarkerColor(j);
check(distinct,"eight legend slots use distinct colors");
#endif
const auto writes = markerBrushWrites;
DecorateIngredientRow(&row);
check(markerBrushWrites == writes, "idle redraw does not rewrite unchanged marker brushes");
g_columnDy = 3;
DecorateIngredientRow(&row);
check(widgets.begin()->second.y == 3, "marker position follows column geometry changes");
const auto colourOrder = SortOrder({
    {"Alpha", 1, 0, 0, {}, {1, 2}},
    {"Bravo", 1, 0, 0, {}, {0}},
    {"Charlie", 1, 0, 0, {}, {0, 2}},
    {"Delta", 1, 0, 0, {}}
}, SortKey::Colors, true);
check(colourOrder == std::vector<int>{2, 0, 1, 3}, "color sort uses count then earliest selected position");
check(MarkerColor(11) != MarkerColor(7), "twelfth color is available");
const auto allocated=widgets.size();
text.text="Other Herb";g_markerEffects[MatchKey(text.text)]={"Nighteye"};DecorateIngredientRow(&row);
check(visible()==1,"recycled row hides three stale circles");
check(widgets.size()==allocated,"named marker widgets are reused");
double singleRight=-1e6;for(auto& [id,w]:widgets)if(w.visible)singleRight=std::max(singleRight,w.x+w.w);
check(visible()==1 && std::abs(singleRight-right)<0.001,"one and four markers share the right edge");
text.text="Unknown Herb";g_markerEffects[MatchKey(text.text)]={"Unknown"};DecorateIngredientRow(&row);
check(visible()==0,"unselected effect has no visible legend color");
g_markerNames.clear();text.text="[Ingr] Test Herb";DecorateIngredientRow(&row);
check(visible()==0,"clear all hides all ingredient circles");
std::cout<<checks<<" checks, "<<failed<<" failures\n";return failed?1:0;
}
'''
helpers = src[src.index('struct IngredientMarkerStyle'):src.index('void HideOwn()')]
for signature in ['UE::UObject* EffectCircle(', 'void DecorateIngredientMarkers(']:
    helpers += source_paths.function(src, signature)
cpp = out / 'alchemy_effect_markers_tests.cpp'
cpp.write_text(prefix + helpers + source_paths.function(src, 'void DecorateIngredientRow(') + main, encoding='utf-8')
exe = out / 'alchemy_effect_markers_tests.exe'
subprocess.run(['cl','/nologo','/std:c++latest','/EHsc','/utf-8',f'/I{root / "src"}',str(cpp),f'/Fe:{exe}',f'/Fo:{out}/'],check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
