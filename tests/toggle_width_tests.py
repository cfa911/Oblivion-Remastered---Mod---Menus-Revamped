"""Compile the actual header factory and its production call sites with layout doubles."""
from pathlib import Path
import subprocess
import sys
import source_paths

root=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]).resolve(); out.mkdir(parents=True,exist_ok=True)
source=(root/'src/sorting/PickerHeader.cpp').read_text()
factory=source_paths.function(source,'const auto make =')+';'
start=source.index('// Register custom headers in')
end=source.index('carousel = reflect::Hold(navigation);',start)
calls=source[start:end]
prefix=r'''
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <iostream>
struct Widget { float x=0,y=0,w=0,h=0,widthOverride=0,heightOverride=0; };
struct Record { std::wstring name; Widget* widget; };
std::vector<Record> records;
namespace UE { using UObject=Widget; }
namespace reflect { using Handle=Widget*; Widget* Get(Widget* p){return p;} Widget* Hold(Widget* p){return p;} }
std::vector<reflect::Handle> g_ownedHeaders;
namespace ui {
Widget* CreateWidget(const wchar_t*) { return new Widget; }
Widget* RootCanvas(Widget*,const wchar_t*) { return new Widget; }
void Float(Widget* p,const wchar_t* name,float value) {
if(std::wstring(name)==L"SetWidthOverride")p->widthOverride=value;else p->heightOverride=value;
}
void RoundedBox(Widget*,bool,const float*,const float*,int){}
struct Call {
Call(Widget*,const wchar_t*){}
int Size(const char*){return 8;} bool Set(const char*,Widget*){return true;} bool RunGuarded(){return true;}
};
}
Widget* CanvasChild(Widget*,const wchar_t*,const wchar_t* name,float x,float y,float w,float h,int=0){
auto* p=new Widget{x,y,w,h}; records.push_back({name,p});return p;
}
void Brush(Widget*,const wchar_t*){}
namespace enchantedfilter { bool Supports(int){return true;} }
namespace stolenfilter { bool Supports(int){return true;} }
namespace mrconfig { struct PickerOn { bool inventory = true, magic = true, container = true, stolenFilter = true, enchantedFilter = true; }; struct Values { PickerOn picker; }; inline const Values& Get() { static Values v; return v; } }
namespace itemtags { struct Data{const wchar_t* icons[1]={L"Category.png"};}; Data Info(int){return {};}}
void PaintEnchanted(){} void PaintStolen(){}
namespace itemsort { enum class Key { Name, Health, Damage, Armor, Weight, Value, Cost, EffectValue, Category, School, Magnitude, kCount }; const wchar_t* TexturePath(Key){return L"tex";} constexpr const wchar_t* kActiveBackground=L"back"; std::vector<Key> MagicKeys(){return {Key::Name,Key::Cost};} std::vector<Key> ActiveKeys(){return {Key::Name,Key::EffectValue,Key::Category,Key::School,Key::Magnitude};} }
bool TextureBrush(Widget*,const wchar_t*){return true;}
void BrushHeader(const wchar_t*){}
constexpr int kSorts=11; bool OwnSorts(){return true;}
reflect::Handle sortHeaders[11]{},sortImages[11]{},sortFrames[11]{},sortArrows[11]{},sortBacks[11]{};
bool Create(){
std::wstring classPath=L"Class",suffix=L"1";
Widget navigationWidget;auto* navigation=&navigationWidget;
reflect::Handle enchantedHeader{},enchantedImage{},enchantedFrame{},stolenHeader{},stolenImage{},stolenFrame{},header{},headerImage{},headerFrame{};
int kind=0,tab=0;Widget* box=nullptr;   // the Inventory picker: all six sorts
constexpr float kClear[4]={};
'''
main=r'''
return true;
}
int main(){
int checks=0,failed=0;
auto check=[&](bool ok,const char* why){++checks;if(!ok){++failed;std::cout<<"FAIL "<<why<<"\n";}};
check(Create(),"factory succeeds");
auto find=[](const std::wstring& name){for(auto& r:records)if(r.name==name+L"1")return r.widget;return static_cast<Widget*>(nullptr);};
for(const auto* prefix:{L"SlEnchantedHeader",L"SlStolenHeader",L"SlPieceHeader"}){
const bool toggle=std::wstring(prefix)!=L"SlPieceHeader";
auto* size=find(std::wstring(prefix)+L"Size");auto* frame=find(std::wstring(prefix)+L"Frame");auto* image=find(std::wstring(prefix)+L"Icon");
check(size&&frame&&image,"size/frame/image created");if(!size||!frame||!image)continue;
const float width=toggle?30.0f:49.0f;
check(std::abs(size->w-width)<0.001f&&std::abs(size->widthOverride-width)<0.001f,"toggle width equals artwork width, category stays 49");
check(frame->w==size->w&&frame->h==49&&size->h==49&&size->heightOverride==49,"frame follows width and retains height");
check(image->x>=0&&image->y>=0&&image->x+image->w<=size->w&&image->y+image->h<=49,"image fits button");
check(std::abs(image->w/image->h-39.0f/40.0f)<0.001f,"artwork aspect ratio preserved");
check(toggle?image->w<39:(image->w==39&&image->h==40&&image->x==5&&image->y==4),"only toggle artwork shrinks");
if(toggle){
check(image->x==0&&std::abs(image->w-size->w)<0.001f,"toggle artwork fills horizontal button width");
check(std::abs(image->w-30.0f)<0.001f&&std::abs(image->h-40.0f*30.0f/39.0f)<0.001f,"visible toggle width is 30 with proportional height");
}
}
for(int k=0;k<6;++k){
const std::wstring prefix=L"SlSort"+std::to_wstring(k);
auto* size=find(prefix+L"Size");auto* image=find(prefix+L"Icon");auto* back=find(prefix+L"Back");auto* arrow=find(prefix+L"Arrow");
check(size&&image&&back&&arrow&&sortHeaders[k],"own sort button with icon, active background and arrow");
if(size&&back&&arrow&&image){const float want=k==5?82.0f:49.0f;
check(size->w==want&&back->w==want&&back->h==49,"sort buttons 49 wide; Value 82 = native Price 87 minus margins, background fills it (owner 2026-11-07)");
check(image->w==39&&image->h==40&&std::abs(image->x+image->w/2-want/2)<0.001f,"icon keeps its size and is centred in its button (Value over the price column, owner 2026-11-07)");
check(std::abs((arrow->x)-(image->x+30))<0.001f,"arrow stays at the icon's lower right corner");
check(arrow->x+arrow->w<=size->w&&arrow->y+arrow->h<=49,"arrow inside the button");}
}
std::cout<<checks<<" checks, "<<failed<<" failures\n";return failed?1:0;
}
'''
cpp=out/'toggle_width.cpp';exe=out/'toggle_width.exe'
cpp.write_text(prefix+factory+calls+main,encoding='utf-8')
subprocess.run(['cl','/nologo','/EHsc','/std:c++latest',str(cpp),f'/Fe:{exe}',f'/Fo:{out}/'],check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
