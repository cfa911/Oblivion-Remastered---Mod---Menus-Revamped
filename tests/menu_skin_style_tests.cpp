// Menu skin styling is separate from logic (owner, 2026-11-09): one table per preset. Only Dark recolours; the shared
// layout and behaviour changes run in every preset. Container and barter menus reuse the same table.
#include <cstdio>
#include "../src/core/UiTheme.h"
#if __has_include("../src/core/DarkStyle.h")
#include "../src/core/DarkStyle.h"
#else
// Before the batch (owner 2026-11-10): no shared Dark style values; colours were scattered literals.
namespace darkstyle {
struct Rgb { float r, g, b; };
struct Style { Rgb panel{ 0, 0, 0 }, detailsPage{ 0, 0, 0 }, rowText{ 0, 0, 0 }, rowSeparator{ 0, 0, 0 }, columnLine{ 0, 0, 0 }, rowIcon{ 0, 0, 0 }, headerIcon{ 0, 0, 0 }; };
inline const Style& Get() { static const Style s{}; return s; }
inline float Linear(float) { return -1; }
}
#endif
#include <string_view>
#include <cmath>
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
#ifndef UISTYLE_MAGIC_SKIN
// Pre-phase-2 behaviour: only the Inventory (kind 0) took the dark skin; Magic kept the preset palette.
namespace uistyle {
inline constexpr bool SkinnedKind(int kind) { return kind == 0; }
inline constexpr const Search& SearchFor(bool, Preset p) { return Get(p).search; }
inline constexpr const Picker& PickerFor(bool inventory, Preset p) { return inventory && SkinFor(p).recolor ? kInventorySkinPicker : Get(p).picker; }
inline constexpr bool IsMagicPage(std::string_view) { return false; }
}
#endif
#ifndef DARKSTYLE_TABS_AND_DETAIL
// Round 6: tab greys baked into the art script.
namespace darkstyle { struct TabValues { float selected = 0, unselected = 0, gap = 0, outline = 0, glyph = 0, glyphDim = 0; };
inline TabValues Tabs() { return {}; } }
#else
namespace darkstyle { inline Style::Tab Tabs() { return Get().tab; } }
#endif
#ifndef DARKSTYLE_INVENTORY_PAGE
// Round 7: Magic pages were detail pictures tinted by panel 38; no frame value; the details page was our own box.
namespace testpage { inline float Frame() { return -1; } inline float DetailsTint() { return -1; } inline bool PageUntinted(const uistyle::Skin&) { return false; } }
#else
namespace testpage {
inline float Frame() { return darkstyle::Get().frame.r; }
inline float DetailsTint() { return darkstyle::Get().detailsPage; }
inline bool PageUntinted(const uistyle::Skin& s) { for (int i = 0; i < s.artSwapCount; ++i) if (std::string_view(s.artSwaps[i].stock) == "T_MagicBackground") return false; return true; }   // RMI-091: baked, no swap
}
#endif
#ifndef UISTYLE_MAGIC_SKIN_R2
// Round 1 behaviour: Magic header icons dimmed while inactive; the details page (paper material) never tinted.
namespace uistyle {
inline constexpr float MagicHeaderOpacity(Preset, float stock) { return stock; }
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
 // decision 28 (28a): the equipped scribble is cleared in the Custom pack (T_UI_Scribble), no run-time flag
#ifdef UISTYLE_SKIN_ART
 const auto swaps=[](const Skin& s,std::string_view stock){for(int i=0;i<s.artSwapCount;++i)if(stock==s.artSwaps[i].stock&&s.artSwaps[i].file)return true;return false;};
 Check(classic.artSwapCount==0,"the parchment presets keep the game's art");
 Check(!swaps(dark,"T_List_Default")&&!swaps(dark,"T_List_equip_V2"),"row separators and the equipped band are baked in the Custom pack, not swapped (decision 28)");
 // decision 28 (28b): the item details art is baked in the Custom pack (stat icons, bands, sigil icons, button art, ring centre)
 Check(!swaps(dark,"T_Property_1_magnitude")&&!swaps(dark,"T_Property_1_Range")&&!swaps(dark,"T_Property_1_time")&&!swaps(dark,"T_Property_1_cost"),
  "effect stat icons are baked, not swapped");
 Check(!swaps(dark,"T_inv_details_effects_title_BG")&&!swaps(dark,"T_SigilStone_SubNameBG"),"title and sigil header bands are baked, not swapped");
 Check(!swaps(dark,"T_Category_Armor")&&!swaps(dark,"T_Category_Weapons"),"sigil header icons are baked, not swapped");
 Check(!swaps(dark,"T_Arrow_Empty")&&!swaps(dark,"T_Button_State_Hovered_LeftLineFade"),"action button arrows and rules are baked (RMI-087), not swapped");
 Check(!swaps(dark,"T_CircularProgress_bg"),"skill ring centre is baked, not swapped");
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
 // Phase 2 (owner 2026-11-10): the Magic menu takes the same Dark look; Classic and Light keep their palettes.
 Check(SkinnedKind(0)&&SkinnedKind(1)&&SkinnedKind(2)&&SkinnedKind(5)&&SkinnedKind(6),
  "Inventory and every Magic picker (School, Effect, Type, Active Effects) use the menu skin");
 // C.1 (owner 2026-11-12): the container/barter sides take the Inventory bar, its look included.
 Check(SkinnedKind(3)&&SkinnedKind(4),"container/barter sides use the Inventory bar skin");
 Check(!SkinnedKind(-1),"inactive pickers keep the preset palette");
 Check(&SearchFor(true,Preset::Dark)==&kInventorySkinSearch,"the Magic search bar in Dark is the Inventory's charcoal bar");
 Check(&SearchFor(true,Preset::Classic)==&kClassic.search&&&SearchFor(false,Preset::Dark)==&kDark.search,
  "Classic keeps its bar and unskinned menus keep the Dark palette bar");
 Check(&PickerFor(SkinnedKind(1),Preset::Dark)==&kInventorySkinPicker&&&PickerFor(SkinnedKind(5),Preset::Dark)==&kInventorySkinPicker,
  "Magic School and Type popups use the Inventory's dark popup in Dark");
 Check(&PickerFor(SkinnedKind(1),Preset::Classic)==&kClassic.picker,"Magic popups keep the Classic popup in Classic");
 // RMI-091 round 3: the Magic page (T_MagicBackground) is baked into MRCustom_P; no per-widget page swap (IsMagicPage removed)
 // Batch 2026-11-10: one Dark style header; Inventory and Magic read the same values; PNGs are white shapes tinted by them.
 const auto& ds=darkstyle::Get();
 const auto same=[](darkstyle::Rgb c,float v){return c.r==v&&c.g==v&&c.b==v;};
 Check(same(ds.panel,41),"the page body is the installed Inventory panel art's (RGB 41, MRSkinPlayerPanel_P)");
 Check(same(ds.columnLine,99),"the row column lines are the Inventory pak's gray (flat RGB 99)");
 Check(same(ds.headerIcon,247),"the header sort icons are the Inventory pak's white (RGB 247)");
 Check(ds.rowText.r>200,"light row text");
 Check(std::abs(darkstyle::Linear(ds.rowText.r)-dark.ink[0])<0.002f,"the skin's row ink comes from the shared row text value");
#ifdef UISTYLE_SKIN_ART
#ifdef DARKSTYLE_TINTED_ART
 const auto tinted=[](const Skin& s,std::string_view stock,darkstyle::Value want){for(int i=0;i<s.artSwapCount;++i)if(stock==s.artSwaps[i].stock)return s.artSwaps[i].tint==want;return false;};
 (void)tinted;
 const auto swapped=[](const Skin& s,std::string_view stock){for(int i=0;i<s.artSwapCount;++i)if(stock==s.artSwaps[i].stock)return true;return false;};
 Check(!swapped(dark,"T_Line_V"),"row column lines come from the Dark pack (T_Line_V baked in columnLine, no run-time swap)");
#else
 Check(false,"row column lines come from the Dark pack (T_Line_V baked in columnLine, no run-time swap)");
#endif
#endif
 // Phase 2 (owner 2026-11-10): the Dark skin's run-time tints are named in DarkStyle.h, not literals in UiTheme.h.
#ifdef DARKSTYLE_SKIN_TINTS
 const auto& dt=darkstyle::kDefaults.skin;
 Check(dark.rule==dt.rule&&dark.guide==dt.guide&&dark.ring==dt.ring&&dark.ornament==dt.ornament&&dark.pattern==dt.pattern
  &&dark.band==dt.band&&dark.buttonFill==dt.buttonFill&&dark.emphasisInk==dt.emphasisInk&&dark.buttonRule==dt.buttonRule
  &&dark.scrollTrack==dt.scrollTrack&&dark.scrollBorder==dt.scrollBorder&&dark.scrollInside==dt.scrollInside,
  "kDarkSkin takes every tint from DarkStyle.h");
 Check(dark.rule==Color{0.65f,0.65f,0.65f,0.55f}&&dark.guide==Color{0.55f,0.55f,0.55f,0.13f}&&dark.band==Color{0.41f,0.65f,1.0f,1}
  &&dark.scrollInside==0.0222f&&dark.buttonRule==Color{0.83f,0.83f,0.83f,1},"the tint values are unchanged by the move");
#else
 Check(false,"kDarkSkin takes every tint from DarkStyle.h");
#endif
 // Round 2 (owner screenshot 2026-11-10): Effect, Type and Cost white like the A-Z and School icons; the details page dark.
 Check(MagicHeaderOpacity(Preset::Dark,0.5f)==1.0f,"Dark Magic header icons are full white even while inactive");
 Check(MagicHeaderOpacity(Preset::Classic,0.5f)==0.5f&&MagicHeaderOpacity(Preset::Light,0.45f)==0.45f,
  "Classic and Light keep the dimmed inactive header icons");
 // Round 7 (owner 2026-11-10): Magic tabs follow the Inventory tab process; pages keep their detail around the header colour.
 const auto tabs=darkstyle::Tabs();
 Check(tabs.selected==41&&tabs.unselected==33&&tabs.gap==24&&tabs.outline==134&&tabs.glyph==242&&tabs.glyphDim==162,
  "the tab greys are the Inventory compiler's (BODY 41, BOX 33, GAP 24, LINE 134, glyphs 242/162), named in DarkStyle.h");
 // Round 8 (owner 2026-11-10, "not using the same method"): the Inventory method for the Magic pages.
 Check(testpage::Frame()==16,"the frame is the installed Inventory panel art's (RGB 16), named in DarkStyle.h");
 Check(testpage::PageUntinted(dark),"the Magic page is baked into the pack (RMI-091): no run-time swap or tint");
 Check(testpage::DetailsTint()==dark.pageGray&&dark.pageGray==0.010f,
  "one details page tint (DarkStyle.h detailsPage) for the Inventory and the Magic page widgets, the Inventory's 0.010");
std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
