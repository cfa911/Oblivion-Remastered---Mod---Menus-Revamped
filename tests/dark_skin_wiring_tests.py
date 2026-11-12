"""RMI-068 and RMI-071 round 3: wiring of the dark Inventory scrollbar and details passes (source checks)."""
import sys
import source_paths

src = source_paths.read("picker")
fails = []
count = 0
def check(ok, why):
    global count
    count += 1
    if not ok:
        fails.append(why)

# Phase 7 (owner decision 18): the scrollbar colours are baked into the stock scrollbar materials; no run-time painter
check("PaintScrollbarStyle" not in src, "the run-time scrollbar painter is removed (baked in the Custom pack)")
details = source_paths.function(src, "void StyleDetails(")
check(not details and "WidgetBarStyle" not in src and "PageTint(" not in src,
      "phase 28b (decision 28): the item details are baked in the Custom pack; no run-time details pass (page tint, scrollbar, sub-tab art)")
rows = source_paths.function(src, "void StyleRow(")
# decision 28 (28a): the scribble (T_UI_Scribble cleared), row ink and row art are baked in the Custom pack: no Inventory row pass
check(not rows and "inv_entry_scribble" not in src, "the equipped pen scribble is baked out, not hidden at run time (RMI-072, decision 28)")
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
check(x >= 430 and 910 <= x + w <= 920,
      "the search bar starts after the whole title and keeps its right edge (owner screenshot on a593c0f7: it covered the last s)")
check("DisableTextColor" not in src and "EnableTextColor" not in src,
      "phase 28b (decision 28): the item details are baked in the Custom pack; the perk line colours are the baked class defaults of WBP_ModernMenu_Stats_LevelDescription")
headers = (source_paths.ROOT / "src/sorting/InventoryHeaders.cpp").read_text(encoding="utf-8-sig")
check(not any(s in headers for s in ("kProbeInk", "detailsskin::k", "ruleInk", "0.65f,0.65f")),
      "the Inventory layout code holds no skin colours (styling separate from logic)")
check("skin.recolor" in source_paths.function(src, "void StyleMagicRow("), "every remaining recolour is gated by the preset's skin")
check("SwapArt(" in source_paths.function(src, "void StyleRowArt("), "Magic rows take the remaining art swaps (row icons; separators and equipped band are baked)")
check(not source_paths.function(src, "void StyleDetailsButton("),
      "phase 28b (decision 28): the item details are baked in the Custom pack; the action button is the baked WBP_ModernPrefab_Button (RMI-087)")
check('L"Color"' not in src and "Role::Ring" not in src, "phase 28b (decision 28): the item details are baked in the Custom pack; the skill ring colour is baked (MIC_UI_SkillCircularProgress)")
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
popup = (source_paths.ROOT / "src/sorting/PickerPopup.cpp").read_text(encoding="utf-8-sig")
check("SkinFor(" in popup, "the Inventory popup and toggle looks follow the preset")
check(not any(source_paths.function(src, f) for f in ("void OnDetailsEntryEvent(", "void StyleDetailsEntry(", "unsigned StyleLevelDescription(")),
      "phase 28b (decision 28): the item details are baked in the Custom pack; no effect-line or perk-line pass")
rebuild = source_paths.function(src, "bool RebuildEmphasis(")
check(all(s in rebuild for s in ("SetTextStyleSet", "VisitRows(", "original")),
      "the rich-text row rebuild stays for the Magic Spell Effectiveness line (28c)")
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
check(not source_paths.function(src, "void StyleListEntries("), "phase 28b (decision 28): the item details are baked in the Custom pack; effect lines are baked (WBP_LegacyMenu_AddedEffects)")
check("IsEmphasisRow(" in rebuild and "perkInk" in rebuild, "bold rows take the emphasis gray, regular rows the light perk ink")
try:
    from PIL import Image
    line = Image.open(source_paths.ROOT / "assets/Skin/ButtonLine.png").convert("RGBA")
    seen = [p for p in line.get_flattened_data() if p[3] > 0]
    check(min(p[0] for p in seen) >= 225 and all(p[0] == p[1] == p[2] for p in seen),
          "action button rules are near-white neutral (170 gray read brownish over the laurels)")
except ImportError:
    check(False, "Pillow is needed to check the button rule art")
check(not source_paths.function(src, "void WatchDetails(") and "OnEffectLineEvent" not in src and "ReadBrushTint" not in src,
      "phase 28b (decision 28): the item details are baked in the Custom pack; no details watch, effect-line hook or brush-tint reader")
layout_fn = source_paths.function(src, "bool Picker::LayoutInventoryHeaders(")
place_title = source_paths.function(src, "void PlaceTitleLabel(")
check("PlaceTitleLabel(" in layout_fn, "the Inventory layout places our own tab title label (RMI-073)")
check(all(s in place_title for s in ("TitleLabel(", "/Script/UMG.TextBlock", "SetFont", "uix::SetText", "SetRenderOpacity")),
      "the title label copies the native font and text and hides the clipped native text (RMI-073)")
clear_fn = source_paths.function(src, "void Picker::ClearInventoryHeaderLayout(")
check("titleLabel" in clear_fn and "RemoveFromParent" in clear_fn,
      "leaving the layout removes our title label and shows the native title again")
check("NeutralBrush(" not in src and "WriteBrushTint(" not in src,
      "phase 28b (decision 28): the item details are baked in the Custom pack; the ornament and button rule brush tints are baked literals")
theme = (source_paths.ROOT / "src/core/UiTheme.h").read_text(encoding="utf-8-sig")
check("buttonRule" in theme, "the rule colour of the action button is per preset")
title_event = source_paths.function(src, "void OnTitleEvent(")
keep = source_paths.function(src, "void KeepTitleLabel(")
check("KeepTitleLabel(" in title_event and "uix::SetText" in keep and "SetRenderOpacity" in keep,
      "the title label takes the new name and the native text stays hidden inside the prefab's own SetText (double text or flash on a593c0f7)")
check("menuskin::SetTitleLabel(" in place_title, "the layout hands our title label to the skin")
check("titleNative=reflect::Hold(title)" in place_title and 'ui::Float(title,L"SetRenderOpacity",0.0f)' in place_title,
      "suppress the complete native title prefab while the overlay is visible")
check('"T_Line_02"' not in src, "phase 28b (decision 28): the item details are baked in the Custom pack; T_Line_02 is baked (skin_flat, shared: Sigil Stone, Recharge, Training, Alchemy)")
tick = source_paths.function(src, "void Picker::Tick(")
check("menuskin::RefreshInventoryTitle(" in tick and tick.index("RefreshInventoryTitle(") < tick.index("now >= nextLayout"),
      "the displayed title follows native text even when the prefab bypasses the reflected SetText hook")
check("RefreshButtonRules" not in tick, "phase 28b (decision 28): the item details are baked in the Custom pack; no button rule refresh per tick")
# ---- Phase 2: Dark Magic (owner 2026-11-10) ----
skin_hdr = (source_paths.ROOT / "src/sorting/MenuSkin.h").read_text(encoding="utf-8-sig")
check("WBP_ModernMenu_MagicDetails_C" in skin_hdr and "kMagic" in skin_hdr, "the skin has a Magic target (details class)")
magic_row = source_paths.function(src, "void StyleMagicRow(")
check(bool(magic_row) and "skin.recolor" in magic_row and "StyleRowArt(" in magic_row,
      "Magic rows take light ink and the row art swaps, gated by the preset")
check("OnMagicRowBound" in layout and "WatchDetails" not in layout,
      "the Magic header layout hooks its rows (generated, not in the menu tree); its details are baked (phase 28b)")
check("StyleListScrollbar" not in layout, "the Magic list scrollbar is not painted at run time (baked)")
page = source_paths.function(src, "void StyleMagicPage(")
check(bool(page) and "IsMagicPage(" in page and "SwapArt(" in page, "the Magic page takes its white-shape art in the panel colour, per widget")
check("menuskin::StyleMagicPage(" in layout, "the Magic layout applies the page tint")
bar = (source_paths.ROOT / "src/search/SearchBar.cpp").read_text(encoding="utf-8-sig")
check("SearchFor(" in bar and "menus::Menu::kMagic" in source_paths.function(bar, "const uistyle::Search& BarStyle("),
      "the Magic search bar uses the skinned bar colours")
check("PickerFor(uistyle::SkinnedKind(kind)" in popup and "const auto& cellFill = uistyle::SkinnedKind(kind)" in popup,
      "Magic popups and their cells use the skinned popup look")
hdr_src = (source_paths.ROOT / "src/sorting/PickerHeader.cpp").read_text(encoding="utf-8-sig")
check("SkinnedKind(kind)" in hdr_src, "Magic header icons and the active-sort background use the dark art in Dark")
check("IsDescriptionRule" not in src, "phase 28b (decision 28): the item details are baked in the Custom pack; the description rules (T_Line_Brown, T_Diamond_Brown_Separator, T_Character_Line_Long) are baked")
own_sort = (source_paths.ROOT / "src/sorting/OwnSort.cpp").read_text(encoding="utf-8-sig")
check(own_sort.count("MagicHeaderOpacity(") >= 2, "both the Magic sort icons and the Effect/Type/School picker icons use the Dark Magic opacity")
check("SpellEffectiveness" in page and "InkRich(" in page, "the Spell Effectiveness line (rich text) takes the light ink")
check('"BackgroundPaper"' not in page, "phase 3: the Magic details paper is the game's own (the pack bakes its parchment); not looked up")
rebuild_now = source_paths.function(src, "bool RebuildEmphasis(")
check("!allRows && !detailsskin::IsWarmInk(color)" in rebuild_now,
      "the Spell Effectiveness rows take the light ink whatever their colour (they failed the warm test on 243ea76d)")
# ---- Dark sort icons as shipped PNGs (owner 2026-11-10: "both icon sets", not the local pak) ----
rules = (source_paths.ROOT / "src/sorting/rules/ItemSort.h").read_text(encoding="utf-8")
dark_files = re.findall(r'L"(DarkSort/[A-Za-z-]+\.png)"', rules)
check("DarkTextureFile(" in rules and "DarkAllFile(" in rules, "the sort rules map each sort key and Magic All to a dark PNG")
check(len(set(dark_files)) >= 5, f"the Magic-only dark sort icons are named (Cost, Effect, Type, School, Magnitude; the Inventory six are baked into MRDark_P): {sorted(set(dark_files))}")
missing_icons = [f for f in set(dark_files) if not (source_paths.ROOT / "assets/Sorting" / f).is_file()]
check(dark_files and not missing_icons, f"every dark sort icon ships in assets/Sorting (missing: {missing_icons})")
try:
    from PIL import Image
    dim = []
    for f in set(dark_files):
        path = source_paths.ROOT / "assets/Sorting" / f
        if path.is_file():
            seen = [q for q in Image.open(path).convert("RGBA").get_flattened_data() if q[3] > 0]
            if not seen or min(min(q[:3]) for q in seen) < 255: dim.append(f)
    check(not dim, f"dark sort icons are pure white shapes (coloured in game from DarkStyle.h): {dim}")
except ImportError:
    check(False, "Pillow is needed to check the dark sort icons")
check("DarkTextureFile(" in hdr_src and "darkArt" in hdr_src, "the sort buttons take the dark PNG in Dark (Inventory and Magic)")
check(popup.count("DarkAllFile(") >= 2, "the Magic School/Type All icons (header and popup) take the dark PNG in Dark")
# ---- Batch 2026-11-10: DarkStyle.h, shared rows, tinted white shapes, details page, Spell Effectiveness ----
dark_h_path = source_paths.ROOT / "src/core/DarkStyle.h"
dark_h = dark_h_path.read_text(encoding="utf-8") if dark_h_path.is_file() else ""
check(all(f"{v}" in dark_h for v in ("panel", "frame", "detailsPage", "rowText", "rowSeparator", "columnLine", "rowIcon", "headerIcon")),
      "DarkStyle.h names every Dark colour (panel, frame, details page, row text, separator, column line, row icon, header icon)")
check('#include "core/DarkStyle.h"' in skin_h, "the theme reads the Dark values from DarkStyle.h")
try:
    from PIL import Image
    for f in ():   # ColumnLine.png: T_Line_V is baked into MRDark_P (phase 2)
        path = source_paths.ROOT / "assets/Skin" / f
        ok = path.is_file() and all(q[:3] == (255, 255, 255) for q in Image.open(path).convert("RGBA").get_flattened_data() if q[3] > 0)
        check(ok, f"{f} ships as a pure white shape (coloured in game)")
except ImportError:
    check(False, "Pillow is needed for the white-shape check")
swap = source_paths.function(src, "bool SwapArt(")
paint_swap = source_paths.function(src, "void PaintSwap(")
check("PaintSwap(" in swap and "tint" in paint_swap and "darkstyle::" in paint_swap, "an art swap takes its colour from DarkStyle.h")
row_art = source_paths.function(src, "void StyleRowArt(")
check(bool(row_art) and "StyleRowArt(" in magic_row, "Magic rows keep the row art pass until 28c")
check("darkstyle::" in hdr_src and "headerIcon" in hdr_src and "headerIcon" in popup, "the white sort icons are tinted by headerIcon")
check("keepRows" in source_paths.function(src, "bool RebuildEmphasis(") and "keepRows" in source_paths.function(src, "bool InkRich("),
      "the Spell Effectiveness table (Magic only) keeps its light rows; restoring them left the text dark (4bb22392)")
# ---- Round 6 (owner 2026-11-10); phase 3: the Magic tab atlases are baked into MRDark_P (no run-time tab swap) ----
check(not any(f'"T_Magic_Tabs_{t}"' in skin_h for t in ("Self", "Touch", "Target", "All", "ActiveEffect")),
      "phase 3: the Magic tab textures come from the pack, not an art swap")
check("OnMagicTabsEvent" not in src, "phase 3: no tab re-swap hook (the baked atlas is the game's own texture)")
tool = (source_paths.ROOT / "tools/make_dark_tab_art.py")
check(tool.is_file() and "DarkStyle.h" in tool.read_text(encoding="utf-8"), "the tab art generator takes panel/headerIcon from DarkStyle.h")
check("MRMAGICDARK rich row" in src, "TEMP-DIAG: the Spell Effectiveness style rows are logged once (it stayed dark on 4c0fd98f)")
# ---- Round 7 (owner 2026-11-10): the Inventory tab process for Magic (phase 3: baked by make_dark_pack.py magic_tab) ----
tab_tool = (source_paths.ROOT / "tools/make_dark_tab_art.py").read_text(encoding="utf-8")
check("T_Category_OnSelf" in (source_paths.ROOT / "tools/menus/magic.json").read_text(encoding="utf-8") and "tabSelected" not in tab_tool and "tab" in tab_tool and "compile-outlined-tabs" in tab_tool,
      "the tab tool ports the Inventory compiler on the standalone T_Category masks")
# ---- Round 8 (owner 2026-11-10: "Inv is still better", "not using the same method"; RMI-082) ----
# The Inventory method: its installed panel art (MRSkinPlayerPanel_P, T_UI_PlayerMenu_Background) is a tone curve on the stock
# brightness (<= 40 -> frame 16, >= 124 -> body 41, linear between: the ornament bar keeps its relief), drawn untinted; its
# details page is the game's own page widget tinted pageGray. No overlay box of ours (it stayed up: RMI-082).
try:
    from PIL import Image
    from collections import Counter
    path = source_paths.ROOT / "assets/Skin/MagicPage.png"
    img = Image.open(path).convert("RGBA") if path.is_file() else None
    opaque = [q[:3] for q in img.get_flattened_data() if q[3] > 0] if img else []
    common = [c for c, _ in Counter(opaque).most_common(2)]
    check(common == [(41, 41, 41), (16, 16, 16)], f"MagicPage.png is the Inventory panel art: body 41, frame 16 ({common})")
    check(bool(opaque) and all(16 <= c[0] <= 41 and c[0] == c[1] == c[2] for c in opaque),
          "MagicPage.png stays inside the Inventory curve (gray 16 to 41)")
    bar = {img.getpixel((x, y))[0] for y in range(0, 60) for x in range(200, 824)} if img else set()
    check(len(bar) >= 10, f"the ornament bar keeps its relief ({len(bar)} grays in the bar)")
except ImportError:
    check(False, "Pillow is needed for the round 8 art checks")
check("PlaceDetailsPage" not in src and "MrMagicDetailsPage" not in src, "no page box of ours on the details' root canvas (RMI-082)")
check("PlaceMagicPage" not in src and "MrDarkDetailsPage" not in src,
      "phase 3: no details page overlay of ours (the pack's dark parchment is drawn by the game's own paper material)")
theme_dark = re.search(r"&darkstyle::Style::panel", skin_h)
check('"T_MagicBackground", L"MagicPage.png" }' in skin_h and not theme_dark, "the Magic page art is drawn untinted (its colours are baked)")
check("detailsPage" in skin_h, "the details page tint (pageGray) comes from DarkStyle.h detailsPage")
tab_tool8 = (source_paths.ROOT / "tools/make_dark_tab_art.py").read_text(encoding="utf-8")
check("frame" in tab_tool8 and "124" in tab_tool8 and "DetailsPage" not in tab_tool8,
      "the page art tool applies the Inventory curve with panel/frame from DarkStyle.h")
# ---- Round 9 (owner 2026-11-10): row icons as white shapes drawn in rowIcon. Phase 3: the six school icons are baked into the
# pack (Magic-only); the 11 _ListSmall type icons stay run-time swaps (Sigil Stone and the effect lists use them too). ----
ROW_ICONS = ["T_MagicMenuIcon_" + n + "_ListSmall" for n in ("Ability", "Disease", "Enchantment", "GreaterPower", "LesserPower", "Poison",
                                                             "Potion", "Scroll", "SpellCustom", "Spell", "WortCraft")]
check(all(f'"{n}", L"MagicRow-{n}.png", &darkstyle::Style::rowIcon' in skin_h for n in ROW_ICONS),
      "every Magic row spell-type icon swaps to a white shape drawn in rowIcon")
check("T_MagicSchoolSimple_" not in skin_h.split("kDarkArt[]{", 1)[-1].split("};", 1)[0], "phase 3: the school icons come from the pack")
try:
    from PIL import Image
    white = True
    for n in ROW_ICONS:
        path = source_paths.ROOT / "assets/Skin" / f"MagicRow-{n}.png"
        white = white and path.is_file() and all(q[:3] == (255, 255, 255) for q in Image.open(path).convert("RGBA").get_flattened_data() if q[3] > 0)
    check(white, "the 11 Magic row type icon PNGs ship as pure white shapes (stock alpha)")
except ImportError:
    check(False, "Pillow is needed for the round 9 art checks")
# ---- Language independence (owner 2026-11-10): the Spell Effectiveness texts are found by widget name, never by their words ----
# Static scan of WBP_ModernMenu_MagicMenu: SpellEffectiveness_Box > Magic_SpellEffectivenes_Value (the line under the bars, the
# game's spelling); Magic_Active_Effectiveness_Box > Magic_Active_SpellEffectiveness_Title (+ rich-text details).
check('"Magic_SpellEffectivenes_Value"' in page and '"Magic_Active_SpellEffectiveness_Title"' in page,
      "the Spell Effectiveness line and title are found by their widget names")
check(not re.search(r'GetText\([^)]*\)\s*\.find\("', src), "no styling decision reads a widget's visible text (other game languages)")
# ---- Round 10 (RMI-083 page fade): retired in phase 3 with the overlay page (the game's own paper draws the baked page) ----
updated = source_paths.function(src, "void OnDetailsUpdated(")
# ---- Round 11 (owner screenshot 2026-11-10: the Magic scrollbar thumb stays pale; the Inventory's is charcoal) ----
layout_src = source_paths.function((source_paths.ROOT / "src/sorting/InventoryHeaders.cpp").read_text(encoding="utf-8"), "bool Picker::LayoutInventoryHeaders(")
magic_block = layout_src.split("if (magic) {   // Dark Magic", 1)[1].split("inventoryheaders::Box canvasBounds", 1)[0] if "if (magic) {   // Dark Magic" in layout_src else ""
first_bind = magic_block.split("pe::WatchPost(row->GetClass(),&OnMagicRowBound)", 1)[0]
check("StyleListScrollbar(" not in first_bind, "the Magic scrollbar is no longer repainted per layout pass (baked, Phase 7)")
check("MRMAGICDARK scrollbar" not in src, "the scrollbar TEMP-DIAG went with the painter (owner decision 18)")
# ---- Round 12 (owner on d635dbf2: page half transparent with a laggy fade; Magic thumb still pale; RMI-083) ----
# Log: OpacityControl is a reveal value (restarts near -0.08 per hover, rises slowly to about 0.4), not an alpha. Static scan:
# WBP_ModernMenu_MagicListView wraps "ListView" (VModernListView, ScrollBarStyle with MIC_UI_ScrollbarVert_Handle_Dark) and has
# no GetListView, so the painter fell back to the wrapper (no ScrollBarStyle) and painted nothing.
check("void StyleListScrollbar(" not in src, "no list scrollbar painter is left (Phase 7: baked)")
# ---- Round 13 (light streak on the selected tab): the strip is forced opaque when baking (dark_pack_tests opaque_rows) ----
# ---- Round 14 (RMI-084: crash painting a freed brush resource, in the Magic menu, on 3d89afae) ----
# The game's details widgets are no longer altered: the retainer was switched off on every details event while the game turned
# it back on (render target released and rebuilt each time). Our own image takes the paper's slot in Infos_Overlay instead
# (ReplaceChildAt: same place and size, behind the details content); the Magic details path runs only for the Magic details.
check("SetRetainRendering" not in page + updated, "the game's details retainer is never switched on or off by the Magic skin (RMI-084)")
check(not updated, "phase 28b (decision 28): the item details are baked in the Custom pack; no details event hook")
check("schoolArt" not in (source_paths.ROOT / "src/core/DarkStyle.h").read_text(encoding="utf-8"),
      "phase 3: no school-drawing strength (the game's paper material draws the school illustration itself)")
unclip16 = source_paths.function(src, "void UnclipTitle(")
check("bRetainRender" in unclip16, "the Inventory title's retainer is switched off only while it is on (no needless rebuilds)")
# ---- Rounds 19-22 (details stat/type icons white). Phase 3: the ten icons are baked into the pack (Magic-only), so the per-hover
# re-swap machinery (material icon swap, tile event hook, menu Tick) is gone. ----
STAT_ICONS = ["T_MagicMenuIcon_" + n for n in ("Cost", "Custom_Spell", "Duration", "Greater_Power", "Lesser_Power", "Magnitude",
                                               "Range", "Scroll", "Spell", "Weight")]
check(not any(f'"{n}", L"MagicIcon-' in skin_h for n in STAT_ICONS), "phase 3: the details stat icons come from the pack")
check(all(n not in src for n in ("SwapIconMaterial", "OnIconTileEvent", "OnMagicMenuTick", "g_magicIcons")),
      "phase 3: no per-hover icon re-swap (the baked texture is what the game sets)")
for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
