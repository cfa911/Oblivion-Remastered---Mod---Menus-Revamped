// Menu skin styling is separate from logic (owner, 2026-11-09): one table per preset. Only Dark recolours; the shared
// layout and behaviour changes run in every preset. Container and barter menus reuse the same table.
#include <cstdio>
#include "../src/core/UiTheme.h"
#include <string_view>
#ifndef UISTYLE_SKIN
// Pre-split behaviour: the dark Inventory skin applied in every preset.
namespace uistyle {
struct Skin { bool recolor, invertToggles, hideEquippedScribble; Color ink, perkInk, rule, guide; const wchar_t* rowSeparatorArt; };
inline constexpr Skin kAlways{ true, true, true, {0.86f,0.86f,0.86f,1}, {0.32f,0.32f,0.32f,1}, {0.65f,0.65f,0.65f,0.55f}, {0.55f,0.55f,0.55f,0.13f}, nullptr };
inline constexpr const Skin& SkinFor(Preset) { return kAlways; }
}
#endif
#ifndef UISTYLE_INVENTORY_CONTROL_PAINT
namespace uistyle {
struct InventoryControlStyle { bool darkArt; float selectedSortTint; };
inline constexpr InventoryControlStyle InventoryControls(bool inventory, Preset) { return { inventory, inventory ? 0.24f : 1.0f }; }
}
#endif
unsigned checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
int main(){
 using namespace uistyle;
 const auto& classic=SkinFor(Preset::Classic); const auto& light=SkinFor(Preset::Light); const auto& dark=SkinFor(Preset::Dark);
 Check(!classic.recolor&&!light.recolor,"Classic and Light keep the game's colours");
 Check(dark.recolor,"Dark recolours the menu");
 Check(!classic.invertToggles&&dark.invertToggles,"inverted filter toggles are a Dark look");
 Check(!classic.hideEquippedScribble&&dark.hideEquippedScribble,"the equipped scribble stays in the parchment presets");
#ifdef UISTYLE_SKIN_ART
 const auto swaps=[](const Skin& s,std::string_view stock){for(int i=0;i<s.artSwapCount;++i)if(stock==s.artSwaps[i].stock&&s.artSwaps[i].file)return true;return false;};
 Check(classic.artSwapCount==0,"the parchment presets keep the game's art");
 Check(swaps(dark,"T_List_Default"),"Dark rows get white separators");
 Check(swaps(dark,"T_List_equip_V2"),"Dark equipped rows get a neutral marker, not the parchment band (RMI-072)");
 Check(swaps(dark,"T_Property_1_magnitude")&&swaps(dark,"T_Property_1_Range")&&swaps(dark,"T_Property_1_time")&&swaps(dark,"T_Property_1_cost"),
  "effect stat icons are neutral in Dark");
 Check(swaps(dark,"T_inv_details_effects_title_BG")&&swaps(dark,"T_SigilStone_SubNameBG"),"title and sigil header bands are neutral in Dark");
 Check(swaps(dark,"T_Category_Armor")&&swaps(dark,"T_Category_Weapons"),"sigil header icons are neutral in Dark");
 Check(swaps(dark,"T_Arrow_Empty")&&swaps(dark,"T_Button_State_Hovered_LeftLineFade"),"action button arrows and rules are neutral in Dark");
 Check(swaps(dark,"T_CircularProgress_bg"),"skill ring centre is neutral in Dark");
 Check(dark.ring[0]==dark.ring[1]&&dark.ring[1]==dark.ring[2]&&dark.ring[0]>0.3f,"skill ring is a light neutral gray in Dark");
#ifdef UISTYLE_SKIN_EMPHASIS
 // owner swatch 2026-11-09: sRGB 99 97 99, the highlighted sub-tab disc (linear about 0.125 0.120 0.125)
 Check(dark.emphasisInk[0]>0.11f&&dark.emphasisInk[0]<0.14f&&dark.emphasisInk[1]<dark.emphasisInk[0]&&dark.emphasisInk[2]==dark.emphasisInk[0],
  "perk emphasis is the highlighted sub-tab gray (owner swatch), not brown");
 Check(classic.emphasisInk[3]==0,"the parchment presets keep the table's emphasis");
#else
 Check(false,"perk emphasis has a Dark colour (not brown)");
#endif
#else
 Check(false,"Dark has an art swap table (stock texture to neutral PNG)");
#endif
 Check(dark.ink[0]>0.8f&&dark.perkInk[0]==dark.ink[0],"perk lines use the light ink; locked ones dim through the game's opacity");
 Check(classic.rule[0]>classic.rule[2]&&classic.guide[0]>classic.guide[2],"Classic header rules are brown ink, not the dark gray");
 Check(dark.rule[0]==dark.rule[2],"Dark header rules are neutral");
#ifdef UISTYLE_SKIN
 Check(dark.pageGray>0&&dark.pageGray<0.022f,"Dark details page is a very dark gray, darker than the list (sRGB 41)");
 Check(dark.pattern[0]==1&&dark.pattern[1]==1&&dark.pattern[2]==1,"laurel pattern shows its packaged gray unchanged");
 Check(dark.scrollInside>dark.scrollBorder&&dark.scrollTrack<0.01f,"dark scrollbar: near-black track and border, charcoal thumb");
#endif
 const auto classicControls = InventoryControls(true, Preset::Classic);
const auto lightControls = InventoryControls(true, Preset::Light);
const auto darkControls = InventoryControls(true, Preset::Dark);
Check(!classicControls.darkArt && classicControls.selectedSortTint == 1.0f,
 "Classic keeps the original status and filter art and the full stock selected-sort background");
Check(!lightControls.darkArt && lightControls.selectedSortTint == 1.0f,
 "Light uses the same original Inventory art");
Check(darkControls.darkArt && darkControls.selectedSortTint == 0.24f && !InventoryControls(false, Preset::Dark).darkArt,
 "only Dark Inventory selects the dark artwork and dimmed sort background");
std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
