// RMI-071: the dark Inventory skin reaches the item details view. Decides, per image brush resource and per text colour,
// what the details pass restyles; shared textures are restyled per widget, never replaced. The colours themselves are
// per preset in uistyle::Skin (menu_skin_style_tests).
#include <cstdio>
#include <string_view>
#include "../src/sorting/rules/DetailsSkin.h"
// Round 16 pre-fix: brush tints were never read or changed (rule middles and ornaments kept their warm brush colour).
#ifndef DETAILSSKIN_BRUSH_TINT
namespace detailsskin { inline bool IsBrushRule(const float*){ return false; } inline void NeutralBrush(float*,float){} }
#endif
unsigned checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
int main(){
 using namespace detailsskin;
 Check(Classify("MIC_MaskedPageRight","")==Role::Page,"right parchment page is the page");
 Check(Classify("MIC_MaskedPageLeft","")==Role::Page,"left parchment page is the page");
 Check(Classify("MaterialInstanceDynamic_12","MIC_MaskedPageRight")==Role::Page,"a runtime instance of the page material is the page");
 Check(Classify("T_StatsMenu_Line_R","")==Role::Ornament,"shared ornament line is tinted, not replaced");
 Check(Classify("T_Inventory_pattern","")==Role::Pattern,"details pattern is tinted neutral");
 Check(Classify("T_inv_details_effects_title_BG","")==Role::Band,"brown title band is neutralised per widget (shared with Stats)");
 // Round 4: the button's animation resets widget tints on selection, so the circle art is darkened in the package
 Check(Classify("T_inv_subtabs_button_BG2","")==Role::None,"sub-tab circle art comes from the texture package, not a widget tint");
 Check(Classify("T_inv_details_preview_frame_D","")==Role::None,"preview frame comes from the texture package");
#ifdef DETAILSSKIN_ROUND6
 Check(Classify("MaterialInstanceDynamic_7","MIC_UI_SkillCircularProgress")==Role::Ring,"the skill ring is recoloured through its Color parameter");
 Check(Classify("T_decoration_line","")==Role::Ornament,"the curly ornament line (white art) takes the ornament tint");
#else
 Check(false,"the skill ring is recoloured through its Color parameter");
 Check(false,"the curly ornament line (white art) takes the ornament tint");
#endif
 Check(Classify("","")==Role::None,"an image without a brush is left alone");
 // owner screenshot on 89d7ca0d: only the bold highlighted words take the emphasis gray; regular perk text stays light
#ifdef DETAILSSKIN_EMPHASIS_ROWS
 Check(IsEmphasisRow("Bold-Bold")&&IsEmphasisRow("Medium-Bold"),"bold style rows are the highlighted words");
 // log on f018ac87: the brown rows of the perk table are Default (normal text) and RT_BlueMed (the highlighted words)
 Check(IsEmphasisRow("RT_BlueMed"),"RT_BlueMed (brown in this game) is the highlighted words");
 Check(!IsEmphasisRow("Regular-Regular")&&!IsEmphasisRow("Default"),"regular and default rows are normal perk text");
#else
 Check(false,"only the bold style rows are the highlighted words");
#endif
 const float brown[]{0.05f,0.03f,0.02f,1}, black[]{0,0,0,1}, white[]{1,1,1,1};
 const float red[]{0.6f,0.02f,0.02f,1}, green[]{0.05f,0.5f,0.05f,1}, faded[]{0.05f,0.03f,0.02f,0.4f};
 Check(IsDarkInk(brown)&&IsDarkInk(black),"dark parchment ink turns light");
 Check(IsDarkInk(faded),"a faded dark ink still turns light (its alpha is kept)");
 Check(!IsDarkInk(white),"light text is left alone");
 Check(!IsDarkInk(red)&&!IsDarkInk(green),"coloured effect text keeps its colour");
 // Round 3: the page keeps M_MaskedPage (its shape leaves the 3D preview open) and is darkened by its widget tint
 Check(kPageKeepsMaterial,"page keeps its stock material so the item preview shows through");
 {
  float page[]{1,1,1,0.9f};
  PageTint(page,0.01f);
  Check(page[0]==0.01f&&page[1]==page[0]&&page[2]==page[0],"page tint is the preset's neutral gray");
  Check(page[3]==0.9f,"page tint keeps the game's alpha");
 }
 // the perk text colour logged on 2cdd2c68 (every line, locked or not): 0.072 0.038 0.028
 const float perk[]{0.072f,0.038f,0.028f,1}, locked[]{0.35f,0.2f,0.1f,1}, light[]{0.86f,0.86f,0.86f,1}, dim[]{0.32f,0.32f,0.32f,1};
 Check(IsWarmInk(perk)&&IsWarmInk(locked),"brown perk text is recognised");
 Check(!IsWarmInk(light)&&!IsWarmInk(dim),"gray inks are not recolored again");
 Check(!IsWarmInk(red)&&!IsWarmInk(green)&&!IsWarmInk(white),"modifier colours and light text keep their colour");
 {
  // census on a593c0f7: rule middles are colour-only images with the stock brown brush tint; the fill has a white one
  const float rule[4]{0.070f,0.038f,0.028f,0.62f}, fill[4]{1,1,1,0.67f}, hidden[4]{1,1,1,0};
  Check(IsBrushRule(rule),"a colour-only image with the stock brown brush tint is a button rule");
  Check(!IsBrushRule(fill)&&!IsBrushRule(hidden),"the white fill brushes of the button are not rules");
  float ornament[4]{0.250f,0.184f,0.112f,1};
  NeutralBrush(ornament,1);
  Check(ornament[0]==1&&ornament[1]==1&&ornament[2]==1&&ornament[3]==1,"the ornament brush tint becomes white (our gray decides)");
  float r[4]{0.070f,0.038f,0.028f,0.62f};
  NeutralBrush(r,0.83f);
  Check(r[0]==0.83f&&r[1]==0.83f&&r[2]==0.83f&&r[3]==0.62f,"a neutral brush keeps the brush alpha");
 }
 std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
