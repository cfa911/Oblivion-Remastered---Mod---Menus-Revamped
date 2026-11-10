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

paint = source_paths.function(src, "unsigned PaintScrollbarStyle(")
check(bool(paint), "one scrollbar painter serves the list and the skill sub-tab")
check("ResetBrushHandle(" in paint, "a swapped brush resource clears the cached Slate handle (RMI-068)")
check("if (track ||" not in paint, "a static track material also gets a dynamic instance (skill sub-tab)")
details = source_paths.function(src, "void StyleDetails(")
check("WidgetBarStyle" in details, "the skill sub-tab ScrollBox bar is painted")
page_case = details.split("case detailsskin::Role::Page:", 1)[1].split("case detailsskin::Role::Ornament:", 1)[0]
check("PageTint(" in page_case and "SetBrushResourceObject" not in page_case, "the page keeps its stock material brush")
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
check(x >= 430 and 910 <= x + w <= 920,
      "the search bar starts after the whole title and keeps its right edge (owner screenshot on a593c0f7: it covered the last s)")
check("DisableTextColor" in src and "EnableTextColor" in src, "perk level entries get Inventory-only text colours")
headers = (source_paths.ROOT / "src/sorting/InventoryHeaders.cpp").read_text(encoding="utf-8-sig")
check(not any(s in headers for s in ("kProbeInk", "detailsskin::k", "ruleInk", "0.65f,0.65f")),
      "the Inventory layout code holds no skin colours (styling separate from logic)")
scroll = source_paths.function(src, "void StyleListScrollbar(")
check(all("skin.recolor" in body for body in (rows, details, scroll)), "every recolour is gated by the preset's skin")
check("StyleRowArt(" in rows and "SwapArt(" in source_paths.function(src, "void StyleRowArt("), "rows take the preset's art swaps (separators, equipped marker)")
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
check("IsEmphasisRow(" in rebuild and "perkInk" in rebuild, "bold rows take the emphasis gray, regular rows the light perk ink")
entry_style = source_paths.function(src, "void StyleDetailsEntry(")
check('"T_Property_1_"' in entry_style and "skin.ink" in entry_style,
      "effect-line stat icons (incl. the _White variants the lines tint brown) take the light ink")
details_body = source_paths.function(src, "void StyleDetails(")
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
brush_tint = source_paths.function(src, "bool ReadBrushTint(")
check(bool(brush_tint) and "TintColor" in brush_tint and "SpecifiedColor" in brush_tint,
      "the brush's own tint can be read (colour-only images draw their brush tint; ColorAndOpacity stayed white)")
layout_fn = source_paths.function(src, "bool Picker::LayoutInventoryHeaders(")
place_title = source_paths.function(src, "void PlaceTitleLabel(")
check("PlaceTitleLabel(" in layout_fn, "the Inventory layout places our own tab title label (RMI-073)")
check(all(s in place_title for s in ("TitleLabel(", "/Script/UMG.TextBlock", "SetFont", "uix::SetText", "SetRenderOpacity")),
      "the title label copies the native font and text and hides the clipped native text (RMI-073)")
clear_fn = source_paths.function(src, "void Picker::ClearInventoryHeaderLayout(")
check("titleLabel" in clear_fn and "RemoveFromParent" in clear_fn,
      "leaving the layout removes our title label and shows the native title again")
button_style = source_paths.function(src, "void StyleDetailsButton(")
check("IsButtonRule(" in button_style and "buttonRule" in button_style and "WriteBrushTint(" in button_style,
      "the button rule middles (colour-only, brown brush tint) take the preset's light rule colour in their brush")
check("NeutralBrush(" in details_body and "WriteBrushTint(" in details_body,
      "the ornament lines' gold brush tint is neutralised before the gray tint")
theme = (source_paths.ROOT / "src/core/UiTheme.h").read_text(encoding="utf-8-sig")
check("buttonRule" in theme, "the rule colour of the action button is per preset")
title_event = source_paths.function(src, "void OnTitleEvent(")
keep = source_paths.function(src, "void KeepTitleLabel(")
check("KeepTitleLabel(" in title_event and "uix::SetText" in keep and "SetRenderOpacity" in keep,
      "the title label takes the new name and the native text stays hidden inside the prefab's own SetText (double text or flash on a593c0f7)")
check("menuskin::SetTitleLabel(" in place_title, "the layout hands our title label to the skin")
check("titleNative=reflect::Hold(title)" in place_title and 'ui::Float(title,L"SetRenderOpacity",0.0f)' in place_title,
      "suppress the complete native title prefab while the overlay is visible")
check("SetBrushTintColor" in source_paths.function(src, "bool WriteBrushTint("),
      "apply live button brush colour through the engine setter")
check('"T_Line_02"' in details and "SigilStone" in details,
      "the thin brown edge below Sigil Stone headings is neutralised only in that details widget")
tick = source_paths.function(src, "void Picker::Tick(")
check("menuskin::RefreshInventoryTitle(" in tick and tick.index("RefreshInventoryTitle(") < tick.index("now >= nextLayout"),
      "the displayed title follows native text even when the prefab bypasses the reflected SetText hook")
check("menuskin::RefreshButtonRules(" in tick and "ReadBrushTint(" in source_paths.function(src, "void RefreshButtonRules("),
      "the active Inventory checks known button rules again after native animation restores the stock tint")
check("ui::TreeWidgets(button, 3000)" in button_style,
      "button painting uses the same widget budget as the census that found the late unhovered rules")
# ---- Phase 2: Dark Magic (owner 2026-11-10) ----
skin_hdr = (source_paths.ROOT / "src/sorting/MenuSkin.h").read_text(encoding="utf-8-sig")
check("WBP_ModernMenu_MagicDetails_C" in skin_hdr and "kMagic" in skin_hdr, "the skin has a Magic target (details class)")
magic_row = source_paths.function(src, "void StyleMagicRow(")
check(bool(magic_row) and "skin.recolor" in magic_row and "StyleRowArt(" in magic_row,
      "Magic rows take light ink and the row art swaps, gated by the preset")
check("OnMagicRowBound" in layout and "menuskin::WatchDetails(a_menu, list, menuskin::kMagic)" in layout,
      "the Magic header layout hooks its rows (generated, not in the menu tree) and its details view")
check("menuskin::StyleListScrollbar(list)" in layout.split("OnMagicRowBound", 1)[-1] or layout.count("StyleListScrollbar(list)") >= 2,
      "the Magic list scrollbar is painted dark")
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
check("IsDescriptionRule" in details and "T_Diamond_Brown_Separator" in (source_paths.ROOT / "src/sorting/rules/DetailsSkin.h").read_text(encoding="utf-8"),
      "the Magic details separators are neutralised like the Inventory description rule")
own_sort = (source_paths.ROOT / "src/sorting/OwnSort.cpp").read_text(encoding="utf-8-sig")
check(own_sort.count("MagicHeaderOpacity(") >= 2, "both the Magic sort icons and the Effect/Type/School picker icons use the Dark Magic opacity")
check("SpellEffectiveness" in page and "InkRich(" in page, "the Spell Effectiveness line (rich text) takes the light ink")
check('"BackgroundPaper"' in page, "the Magic details page is found by its widget name BackgroundPaper (no paper material at run time)")
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
check(bool(row_art) and "StyleRowArt(" in rows and "StyleRowArt(" in magic_row, "Inventory and Magic rows share one row art pass")
check("darkstyle::" in hdr_src and "headerIcon" in hdr_src and "headerIcon" in popup, "the white sort icons are tinted by headerIcon")
check("keepRows" in source_paths.function(src, "bool RebuildEmphasis(") and "keepRows" in source_paths.function(src, "bool InkRich("),
      "the Spell Effectiveness table (Magic only) keeps its light rows; restoring them left the text dark (4bb22392)")
# ---- Round 6 (owner 2026-11-10): details page keeps its parchment outline; dark Magic tabs; Spell Effectiveness evidence ----
try:
    from PIL import Image
    tabs = [source_paths.ROOT / "assets/Skin" / f"MagicTab-{t}.png" for t in ("Self", "Touch", "Target", "All", "ActiveEffect")]
    check(all(t.is_file() for t in tabs), "the five dark Magic tab pictures ship in assets/Skin")
except ImportError:
    check(False, "Pillow is needed for the round 6 art checks")
check(all(f'"T_Magic_Tabs_{t}"' in skin_h for t in ("Self", "Touch", "Target", "All", "ActiveEffect")), "the Magic tab textures are art swaps")
tabs_hook = source_paths.function(src, "void OnMagicTabsEvent(")
check(bool(tabs_hook) and "SwapArt(" in tabs_hook and "OnMagicTabsEvent" in page,
      "the tab picture is re-swapped inside the tabs' own events (no parchment flash on a tab change)")
tool = (source_paths.ROOT / "tools/make_dark_tab_art.py")
check(tool.is_file() and "DarkStyle.h" in tool.read_text(encoding="utf-8"), "the tab art generator takes panel/headerIcon from DarkStyle.h")
check("MRMAGICDARK rich row" in src, "TEMP-DIAG: the Spell Effectiveness style rows are logged once (it stayed dark on 4c0fd98f)")
# ---- Round 7 (owner 2026-11-10): the Inventory tab process for Magic; pages keep their detail; the visible effectiveness line ----
try:
    from PIL import Image
    import statistics
    edges = [0, 260, 432, 603, 774, 1024]
    for i, t in enumerate(("Self", "Touch", "Target", "All", "ActiveEffect")):
        path = source_paths.ROOT / "assets/Skin" / f"MagicTab-{t}.png"
        if not path.is_file():
            check(False, f"MagicTab-{t}.png ships"); continue
        img = Image.open(path).convert("RGBA")
        sel = img.getpixel((edges[i] + 14, 92))[:3]
        other = (i + 1) % 5
        unsel = img.getpixel((edges[other] + 14, 92))[:3]
        check(sel == (41, 41, 41) and unsel == (33, 33, 33),
              f"MagicTab-{t}: flat Inventory tab bodies (selected 41 {sel}, unselected 33 {unsel}), no parchment noise")
except ImportError:
    check(False, "Pillow is needed for the round 7 art checks")
tab_tool = (source_paths.ROOT / "tools/make_dark_tab_art.py").read_text(encoding="utf-8")
check("T_Category_" in tab_tool and "tabSelected" not in tab_tool and "tab" in tab_tool and "compile-outlined-tabs" in tab_tool,
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
place_page = source_paths.function(src, "void PlaceMagicPage(")
check("PlaceMagicPage(" in page and "PageTint(" in place_page and "pageGray" in place_page,
      "the Magic details page is the stock parchment tinted pageGray, like the Inventory page")
theme_dark = re.search(r"&darkstyle::Style::panel", skin_h)
check('"T_MagicBackground", L"MagicPage.png" }' in skin_h and not theme_dark, "the Magic page art is drawn untinted (its colours are baked)")
check("detailsPage" in skin_h, "the details page tint (pageGray) comes from DarkStyle.h detailsPage")
tab_tool8 = (source_paths.ROOT / "tools/make_dark_tab_art.py").read_text(encoding="utf-8")
check("frame" in tab_tool8 and "124" in tab_tool8 and "DetailsPage" not in tab_tool8,
      "the page art tool applies the Inventory curve with panel/frame from DarkStyle.h")
# ---- Round 9 (owner 2026-11-10 screenshot on e77784de: details page still parchment, brown row icons; "Check how inv did theirs") ----
# M_MagicSchoolIcon_paper draws T_SpellPurchase_LeftPageBG through a fixed sampler without vertex colour, so a widget tint
# never reaches it (log: tint set, screen unchanged). The Inventory page is that same parchment times 0.010: the game's page
# image gets the stock parchment as a plain texture and the same tint. Row icons: the Inventory's light-icon recipe (one
# flat value, stock alpha) as white shapes drawn in rowIcon.
ROW_ICONS = ["T_MagicSchoolSimple_" + n for n in ("Alteration", "Conjuration", "Destruction", "Illusion", "Mysticism", "Restoration")] + \
    ["T_MagicMenuIcon_" + n + "_ListSmall" for n in ("Ability", "Disease", "Enchantment", "GreaterPower", "LesserPower", "Poison",
                                                     "Potion", "Scroll", "SpellCustom", "Spell", "WortCraft")]
check(all(f'"{n}", L"MagicRow-{n}.png", &darkstyle::Style::rowIcon' in skin_h for n in ROW_ICONS),
      "every Magic row school and spell-type icon swaps to a white shape drawn in rowIcon")
try:
    from PIL import Image
    white = True
    for n in ROW_ICONS:
        path = source_paths.ROOT / "assets/Skin" / f"MagicRow-{n}.png"
        white = white and path.is_file() and all(q[:3] == (255, 255, 255) for q in Image.open(path).convert("RGBA").get_flattened_data() if q[3] > 0)
    check(white, "the 17 Magic row icon PNGs ship as pure white shapes (stock alpha)")
    path = source_paths.ROOT / "assets/Skin/DetailsPage.png"
    px = [q for q in Image.open(path).convert("RGBA").get_flattened_data() if q[3] > 200] if path.is_file() else []
    mean = [sum(q[i] for q in px) / len(px) for i in range(3)] if px else [0, 0, 0]
    check(bool(px) and mean[0] > 150 and mean[0] > mean[2], f"DetailsPage.png is the stock parchment (the Inventory page's texture), mean {mean}")
except ImportError:
    check(False, "Pillow is needed for the round 9 art checks")
check('SkinArt(L"DetailsPage.png")' in place_page and "SetBrushResourceObject" in place_page,
      "our Magic page image draws the plain parchment (the game's paper material ignores widget tint), then takes pageGray")
# ---- Language independence (owner 2026-11-10): the Spell Effectiveness texts are found by widget name, never by their words ----
# Static scan of WBP_ModernMenu_MagicMenu: SpellEffectiveness_Box > Magic_SpellEffectivenes_Value (the line under the bars, the
# game's spelling); Magic_Active_Effectiveness_Box > Magic_Active_SpellEffectiveness_Title (+ rich-text details).
check('"Magic_SpellEffectivenes_Value"' in page and '"Magic_Active_SpellEffectiveness_Title"' in page,
      "the Spell Effectiveness line and title are found by their widget names")
check(not re.search(r'GetText\([^)]*\)\s*\.find\("', src), "no styling decision reads a widget's visible text (other game languages)")
# ---- Round 10 (owner screenshot on 7018b29d: details page still parchment) ----
# BackgroundPaper (AnimatableRetainerBox) draws the parchment itself: its effect MID M_MagicSchoolIcon_paper_Animated has the
# parameters of M_MagicSchoolIcon_paper (Texture = the retained children). In Dark the retainer stops using it, so the game's
# page image (stock parchment x pageGray, the Inventory page) draws directly; the material's fade (OpacityControl) is copied
# to the retainer's own opacity so the page still fades and hides with the game.
sync_paper = source_paths.function(src, "void SyncMagicPaper(")
check("GetEffectMaterial" in sync_paper and '"OpacityControl"' in sync_paper and "Visible(" in sync_paper,
      "the paper material's OpacityControl drives the retainer's opacity (the game's fade and hide still work)")
updated = source_paths.function(src, "void OnDetailsUpdated(")
check("SyncMagicPaper(" in updated and updated.index("SyncMagicPaper(") < updated.index('event == "Tick"'),
      "the opacity is copied on every details event, per frame included")
# ---- Round 11 (owner screenshot 2026-11-10: the Magic scrollbar thumb stays pale; the Inventory's is charcoal) ----
layout_src = source_paths.function((source_paths.ROOT / "src/sorting/InventoryHeaders.cpp").read_text(encoding="utf-8"), "bool Picker::LayoutInventoryHeaders(")
magic_block = layout_src.split("if (magic) {   // Dark Magic", 1)[1].split("inventoryheaders::Box canvasBounds", 1)[0] if "if (magic) {   // Dark Magic" in layout_src else ""
first_bind = magic_block.split("pe::WatchPost(row->GetClass(),&OnMagicRowBound)", 1)[0]
check("StyleListScrollbar(" in first_bind, "the Magic scrollbar is repainted on every layout pass (the game resets its thumb)")
check("MRMAGICDARK scrollbar" in source_paths.function(src, "void StyleListScrollbar("),
      "TEMP-DIAG: what the Magic scrollbar holds is logged once")
# ---- Round 12 (owner on d635dbf2: page half transparent with a laggy fade; Magic thumb still pale; RMI-083) ----
# Log: OpacityControl is a reveal value (restarts near -0.08 per hover, rises slowly to about 0.4), not an alpha. Static scan:
# WBP_ModernMenu_MagicListView wraps "ListView" (VModernListView, ScrollBarStyle with MIC_UI_ScrollbarVert_Handle_Dark) and has
# no GetListView, so the painter fell back to the wrapper (no ScrollBarStyle) and painted nothing.
check("std::clamp(opacity" not in sync_paper and "kPaperHidden" in sync_paper,
      "the details page is fully shown while the game's reveal runs (no copied fade: RMI-083)")
check('"ListView"' in source_paths.function(src, "void StyleListScrollbar("),
      "the Magic list's inner ListView (no GetListView on the wrapper) takes the Inventory scrollbar paint")
# ---- Round 13 (owner screenshot: a light streak and a broken corner on the selected Active Effects tab) ----
# The stock Magic atlases are transparent in wedges of the strip (about 1000 px each, where the Inventory atlases are opaque);
# the baked tabs kept that alpha, so the room showed through the selected tab's top-left.
try:
    from PIL import Image
    holes = {}
    for t in ("Self", "Touch", "Target", "All", "ActiveEffect"):
        path = source_paths.ROOT / "assets/Skin" / f"MagicTab-{t}.png"
        img = Image.open(path).convert("RGBA") if path.is_file() else None
        holes[t] = sum(1 for y in range(0, 102) for x in range(1024) if img.getpixel((x, y))[3] < 220) if img else -1
    check(all(v == 0 for v in holes.values()), f"the Magic tab strip is opaque like the Inventory's (see-through pixels {holes})")
except ImportError:
    check(False, "Pillow is needed for the round 13 tab check")
# ---- Round 14 (RMI-084: crash painting a freed brush resource, in the Magic menu, on 3d89afae) ----
# The game's details widgets are no longer altered: the retainer was switched off on every details event while the game turned
# it back on (render target released and rebuilt each time). Our own image takes the paper's slot in Infos_Overlay instead
# (ReplaceChildAt: same place and size, behind the details content); the Magic details path runs only for the Magic details.
check("SetRetainRendering" not in place_page + sync_paper and "SetRetainRendering" not in page,
      "the game's details retainer is never switched on or off by the Magic skin (RMI-084)")
# round 15: ReplaceChildAt is not a reflected function in this game (exe has only UOverlay::ReplaceOverlayChildAt)
check('L"GetParent"' in place_page and 'L"ReplaceOverlayChildAt"' in place_page and "GetChildIndex" in place_page and 'L"ReplaceChildAt"' not in place_page,
      "our dark page takes the paper's own slot in Infos_Overlay (same place, behind the details content)")
check("detailsClass" in updated and updated.index("detailsClass") < updated.index("SyncMagicPaper("),
      "the Magic details path runs only for the Magic details widget (Inventory events after a Magic visit took it)")
# ---- Round 16 (owner: "We lost the school animations on the parchment"; Inventory title hardening) ----
# The paper drew the school illustration (T_Illustration_<School>_LeftPage: a white drawing, soft alpha, the right half of the
# page) and faded it in; the reveal value still runs with the paper out of the tree (log on 9325a228: -0.04 -> 0.77 in 0.8 s).
dark_h16 = (source_paths.ROOT / "src/core/DarkStyle.h").read_text(encoding="utf-8")
check("schoolArt" in dark_h16, "DarkStyle.h names the school illustration strength on the dark page")
check('L"AddChildToOverlay"' in place_page and "/Script/UMG.Overlay" in place_page,
      "the paper's slot holds our overlay: the dark page plus the school illustration")
check('L"K2_GetTextureParameterValue"' in sync_paper and "schoolArt" in sync_paper and "reveal" in sync_paper,
      "the illustration is the game's current school texture, faded in with the game's reveal")
unclip16 = source_paths.function(src, "void UnclipTitle(")
check("bRetainRender" in unclip16, "the Inventory title's retainer is switched off only while it is on (no needless rebuilds)")
# ---- Round 17 (owner on e6f9c4cc: no school drawing; log has no "school art" line) ----
# The tile's image draws M_MagicSchoolIcon_Icon (parameter IconTexture); the retainer's paper material composites it.
check('L"IconTexture"' in sync_paper, "the school texture is read from IconTexture (M_MagicSchoolIcon_Icon), Texture as a fallback")
# ---- Round 18 (owner on 9e217d27: the drawing shows but faded only once, and spills past the page's right and top edges) ----
check("OnMagicMenuTick" in page and '"Tick"' in source_paths.function(src, "void OnMagicMenuTick("),
      "the school drawing's fade follows the game's reveal every frame (the Magic menu's Tick)")
check('L"SetPadding"' in sync_paper and "kPageBox" in sync_paper,
      "the drawing fits the right half of the visible page (DetailsPage.png alpha box), not the whole details area")
# ---- Round 19 (owner screenshot: the details stat icons are not white) ----
STAT_ICONS = ["T_MagicMenuIcon_" + n for n in ("Cost", "Custom_Spell", "Duration", "Greater_Power", "Lesser_Power", "Magnitude",
                                               "Range", "Scroll", "Spell", "Weight")]
check(all(f'"{n}", L"MagicIcon-{n}.png", &darkstyle::Style::rowIcon' in skin_h for n in STAT_ICONS),
      "the 10 Magic details stat icons swap to white shapes drawn in rowIcon")
try:
    from PIL import Image
    ok = True
    for n in STAT_ICONS:
        path = source_paths.ROOT / "assets/Skin" / f"MagicIcon-{n}.png"
        ok = ok and path.is_file() and all(q[:3] == (255, 255, 255) for q in Image.open(path).convert("RGBA").get_flattened_data() if q[3] > 0)
    check(ok, "the 10 stat icon PNGs ship as pure white shapes (stock alpha)")
except ImportError:
    check(False, "Pillow is needed for the round 19 icon check")
# ---- Round 20 (owner on eff0b53c: Spell icon not white; Active Effects and a reopened menu show parchment; fade only once) ----
# One dark page per BackgroundPaper (a reopened menu and the Active Effects details have their own; the old page handle stayed
# alive and blocked the new one). The paper goes back into the tree inside our overlay, invisible, so the game's reveal keeps
# running on every hover (out of the tree it ran once). The per-hover type icon is re-swapped every frame.
check("g_magicPapers" in sync_paper and "g_magicPapers" in place_page and "reflect::Get(g_magicPage)" not in place_page,
      "every BackgroundPaper gets its own dark page (reopened menu, Active Effects)")
check(place_page.count('L"AddChildToOverlay"') >= 1 and "SetRenderOpacity" in place_page and "paper" in place_page,
      "the game's paper goes back into the tree inside our overlay, invisible, so its reveal animation keeps running")
check("g_magicIcons" in source_paths.function(src, "void OnMagicMenuTick("), "the details type icon (reset per hover) is re-swapped every frame")
# ---- Round 21 (owner on b2c3e1e7, pack spike: the Magic details page works natively; the Spell type icon stays dark) ----
icon_mat = source_paths.function(src, "bool SwapIconMaterial(")
check(bool(icon_mat) and 'L"IconTexture"' in icon_mat and "SwapIconMaterial(" in source_paths.function(src, "void StyleDetails(")
      and "SwapIconMaterial(" in source_paths.function(src, "void OnMagicMenuTick("),
      "a details icon drawn through a material (texture in IconTexture) takes the white stat icon too, every frame")
# ---- Round 22 (log on 814be2ca: "menu tick seen" never logged: the Magic menu Tick is not a ProcessEvent; the type icon is
# set again by the game on every hover inside its WBP_OriginalImageTile prefab) ----
tile_event = source_paths.function(src, "void OnIconTileEvent(")
check(bool(tile_event) and "SwapArt(" in tile_event and "OnIconTileEvent" in source_paths.function(src, "void StyleDetails("),
      "a recorded details icon is re-swapped right after its own tile widget's events (the type icon is set per hover)")
for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
