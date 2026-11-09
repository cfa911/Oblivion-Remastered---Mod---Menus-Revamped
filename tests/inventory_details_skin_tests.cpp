// RMI-071: the dark Inventory skin reaches the item details view. Decides, per image brush resource and per text colour,
// what the details pass restyles; shared textures are restyled per widget, never replaced.
#include <cstdio>
#include <string_view>
#if __has_include("../src/sorting/rules/DetailsSkin.h")
#include "../src/sorting/rules/DetailsSkin.h"
#else
// Pre-feature behavior: the details view keeps its stock look.
namespace detailsskin {
enum class Role { None, Page, Ornament, Pattern, Band, ButtonBack };
inline Role Classify(std::string_view, std::string_view) { return Role::None; }
inline bool IsDarkInk(const float*) { return false; }
inline bool ScaleTint(float*) { return false; }
inline constexpr float kPatternTint[]{0.5f,0.5f,0.5f,0.12f};
}
#endif
unsigned checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
int main(){
 using namespace detailsskin;
 Check(Classify("MIC_MaskedPageRight","")==Role::Page,"right parchment page becomes the charcoal panel");
 Check(Classify("MIC_MaskedPageLeft","")==Role::Page,"left parchment page becomes the charcoal panel");
 Check(Classify("MaterialInstanceDynamic_12","MIC_MaskedPageRight")==Role::Page,"a runtime instance of the page material is the page");
 Check(Classify("T_StatsMenu_Line_R","")==Role::Ornament,"shared ornament line is tinted, not replaced");
 Check(Classify("T_Inventory_pattern","")==Role::Pattern,"details pattern is tinted neutral");
 // Batch 2 (owner screenshots on 0a0eedaa): brown baked bands are neutralised per widget; the sub-tab circle darkens
 // in proportion to the game's own tint so the selected button stays the brighter one.
 Check(Classify("T_inv_details_effects_title_BG","")==Role::Band,"brown title band is neutralised per widget (shared with Stats)");
 Check(Classify("T_inv_subtabs_button_BG2","")==Role::ButtonBack,"sub-tab circle darkens");
 {
  float selected[]{1,1,1,1},unselected[]{0.6f,0.6f,0.6f,0.8f};
  const bool a=ScaleTint(selected),b=ScaleTint(unselected);
  Check(a&&b&&selected[0]<0.1f&&unselected[0]<selected[0],"circle tint scales down and keeps selected brighter");
  Check(unselected[3]==0.8f,"circle tint keeps its alpha");
  const float again=selected[0];
  Check(!ScaleTint(selected)&&selected[0]==again,"a circle already darkened is not darkened twice");
 }
 Check(kPatternTint[0]==1&&kPatternTint[1]==1&&kPatternTint[2]==1,"laurel pattern shows its packaged gray unchanged");
 Check(Classify("T_inv_details_preview_frame_D","")==Role::None,"preview frame comes from the texture package");
 Check(Classify("","")==Role::None,"an image without a brush is left alone");
 const float brown[]{0.05f,0.03f,0.02f,1}, black[]{0,0,0,1}, white[]{1,1,1,1};
 const float red[]{0.6f,0.02f,0.02f,1}, green[]{0.05f,0.5f,0.05f,1}, faded[]{0.05f,0.03f,0.02f,0.4f};
 Check(IsDarkInk(brown)&&IsDarkInk(black),"dark parchment ink turns light");
 Check(IsDarkInk(faded),"a faded dark ink still turns light (its alpha is kept)");
 Check(!IsDarkInk(white),"light text is left alone");
 Check(!IsDarkInk(red)&&!IsDarkInk(green),"coloured effect text keeps its colour");
 std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
