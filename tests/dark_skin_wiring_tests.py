"""RMI-068 and RMI-071 round 3: wiring of the dark Inventory scrollbar and details passes (source checks)."""
import sys
import source_paths

src = source_paths.read("picker")
fails = []
def check(ok, why):
    if not ok:
        fails.append(why)

paint = source_paths.function(src, "unsigned PaintScrollbarStyle(")
check(bool(paint), "one scrollbar painter serves the list and the skill sub-tab")
check("ResetBrushHandle(" in paint, "a swapped brush resource clears the cached Slate handle (RMI-068)")
check("if (track ||" not in paint, "a static track material also gets a dynamic instance (skill sub-tab)")
details = source_paths.function(src, "void StyleDetails(")
check("WidgetBarStyle" in details, "the skill sub-tab ScrollBox bar is painted")
check("SetBrushResourceObject" not in details, "the page keeps its stock material brush")
check("PageTint(" in details, "the page is darkened by its widget tint")
check("ClassifyWidget(" not in details and "ScaleTint(" not in details,
      "sub-tab art is not tinted per widget (its animation resets the tint; the package darkens it)")
rows = source_paths.function(src, "void StyleRow(")
check("inv_entry_scribble" in rows, "the equipped pen scribble is hidden on every row (RMI-072)")
layout = source_paths.function(src, "bool Picker::LayoutInventoryHeaders(")
check("menuskin::StyleTitle(" in layout and "UnclipTitle(" in source_paths.function(src, "void StyleTitle("),
      "the tab title is not clipped")
unclip = source_paths.function(src, "void UnclipTitle(")
check("SetMinDesiredWidth" in unclip and "ForceLayoutPrepass" in unclip,
      "the title box is wide enough for the longest tab name and re-laid out (RMI-073: it kept the width of Weapons)")
import configparser
ini = configparser.ConfigParser(inline_comment_prefixes=(";",), strict=False)
ini.read(source_paths.ROOT / "config/MenusRevamped.defaults.ini", encoding="utf-8")
x, y, w = (float(ini["Inventory"][k]) for k in ("uX", "uY", "uWidth"))
check(250 <= y <= 300 and x >= 390 and x + w <= 930, "the Inventory search bar sits in the title row, right of the title")
check("DisableTextColor" in src and "EnableTextColor" in src, "perk level entries get Inventory-only text colours")
headers = (source_paths.ROOT / "src/sorting/InventoryHeaders.cpp").read_text(encoding="utf-8-sig")
check(not any(s in headers for s in ("kProbeInk", "detailsskin::k", "ruleInk", "0.65f,0.65f")),
      "the Inventory layout code holds no skin colours (styling separate from logic)")
scroll = source_paths.function(src, "void StyleListScrollbar(")
check(all("skin.recolor" in body for body in (rows, details, scroll)), "every recolour is gated by the preset's skin")
check("SwapArt(" in rows, "rows take the preset's art swaps (separators, equipped marker)")
check("SwapArt(" in details, "the details view takes the preset's art swaps (bands, icons, ring centre)")
button = source_paths.function(src, "void StyleDetailsButton(")
check("SwapArt(" in button, "action buttons take the preset's art swaps (arrows, rules)")
check('L"Color"' in details and "Role::Ring" in details, "the skill ring colour comes from the skin")
unclip_body = source_paths.function(src, "void UnclipTitle(")
check('"text_block"' in unclip_body,
      "the title's inner text (text_block in WBP_AltarTextBlock, log on a22c29b8) gets the width and no-scroll settings")
check("SetScrollingEnabled" in unclip_body,
      "the title does not scroll inside a stale narrow box (RMI-073)")
import re
skin_h = (source_paths.ROOT / "src/core/UiTheme.h").read_text(encoding="utf-8")
files = set(re.findall(r'L"([A-Za-z0-9]+\.png)"', skin_h))
missing = [f for f in files if not (source_paths.ROOT / "assets/Skin" / f).is_file()]
check(bool(files) and not missing, f"every Dark art file is shipped in assets/Skin (missing: {missing})")
check('setSlate("DisableTextColor", skin.perkInk' in src, "locked and unlocked perk lines share the perk ink")
popup = (source_paths.ROOT / "src/sorting/PickerPopup.cpp").read_text(encoding="utf-8-sig")
check("SkinFor(" in popup, "the Inventory popup and toggle looks follow the preset")
entry = source_paths.function(src, "void OnDetailsEntryEvent(")
check("StyleDetailsEntry(" in entry and "SwapArt(" in source_paths.function(src, "void StyleDetailsEntry(") and "OnDetailsEntryEvent" in details,
      "list entries the game rebuilds inside the details view (effect lines) are restyled when rebuilt")
check("Census(" in button, "the action button's parts are measured once (its unhovered rules)")
level = source_paths.function(src, "unsigned StyleLevelDescription(")
rebuild = source_paths.function(src, "bool RebuildEmphasis(")
check("RebuildEmphasis(" in level and all(s in rebuild for s in ("SetTextStyleSet", "emphasisInk", "VisitRows(", "original")),
      "perk emphasis rows are recoloured for the Inventory's widgets only, and the shared table restored")
try:
    from PIL import Image
    band = Image.open(source_paths.ROOT / "assets/Skin/RowEquipped.png").convert("RGBA")
    seen = [p for p in band.get_flattened_data() if p[3] > 0]
    r, g, b = (sum(p[i] for p in seen) / len(seen) for i in range(3))
    check(r > 150 and r > g > b + 40, "the equipped-row band is gold")
    check(max(p[3] for p in seen) >= 100, "the equipped-row band is clearly visible (stronger than the faint 0.22 band)")
except ImportError:
    check(False, "Pillow is needed to check the equipped-row art")
check('"Min Desired Width"' in unclip_body and '"IsScrollingEnable"' in unclip_body,
      "the title prefab's own width and scrolling variables are set, so its SetText keeps them (RMI-073)")
check("OnTitleEvent" in src and "WatchPost" in source_paths.function(src, "void StyleTitle("),
      "the title is re-kept after the prefab's own updates")
check("SetRetainRendering" in unclip_body and "RetainerBox" in unclip_body,
      "the title's animation RetainerBox draws it directly (its effect mask cut the text at a fixed width; RMI-073)")
lists = source_paths.function(src, "void StyleListEntries(")
check("GetDisplayedEntryWidgets" in lists and "StyleDetailsEntry(" in lists and "WatchPost" in lists and "StyleListEntries(" in details,
      "effect lines generated by a list inside the details view are restyled (they are not in its widget tree)")
check("Census(" in source_paths.function(src, "void OnDetailsButtonEvent("),
      "the action button's colours are measured while it is on screen (its rules stayed brown)")
check("IsEmphasisRow(" in rebuild and "perkInk" in rebuild, "bold rows take the emphasis gray, regular rows the light perk ink")
entry_style = source_paths.function(src, "void StyleDetailsEntry(")
check('"T_Property_1_"' in entry_style and "skin.ink" in entry_style,
      "effect-line stat icons (incl. the _White variants the lines tint brown) take the light ink")
check("Census(" in entry_style, "the first effect line is measured once")
details_body = source_paths.function(src, "void StyleDetails(")
check("ornament before tint" in details_body, "the ornament lines' colour before the tint is logged once when shown")
try:
    from PIL import Image
    line = Image.open(source_paths.ROOT / "assets/Skin/ButtonLine.png").convert("RGBA")
    seen = [p for p in line.get_flattened_data() if p[3] > 0]
    check(min(p[0] for p in seen) >= 225 and all(p[0] == p[1] == p[2] for p in seen),
          "action button rules are near-white neutral (170 gray read brownish over the laurels)")
except ImportError:
    check(False, "Pillow is needed to check the button rule art")
watch = source_paths.function(src, "void WatchDetails(")
check("WBP_LegacyMenu_AddedEffects" in watch and "OnEffectLineEvent" in watch,
      "effect lines (WBP_LegacyMenu_AddedEffects, created outside the widget tree) are hooked by class")
check("ShownOnScreen" in source_paths.function(src, "void OnEffectLineEvent("),
      "an effect line is restyled only while the Inventory details view is on screen (the class is shared)")
check("perk row" in rebuild, "the perk table's row names and colours are logged once (bold rows not recognised on 0d025571)")
check("CensusShown(" in src and "CensusShown(" in details_body, "on-screen details images are logged with their positions once")
check("CensusShown(" in source_paths.function(src, "void OnDetailsButtonEvent("),
      "the on-screen census runs from the button's events (the details view was still fading in during the pass)")
census_shown = source_paths.function(src, "void CensusShown(")
check("ShownOnScreen(root)" not in census_shown and "ShownOnScreen(widget)" in census_shown,
      "the on-screen census filters per image (the details root never reports shown; no MR shown lines on d7b00224)")
for why in fails:
    print("FAIL", why)
print(f"46 checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
