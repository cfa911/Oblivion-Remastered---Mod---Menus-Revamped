"""Container and Barter header row (owner, Step C.1 walk-through 2026-11-12: "migrate our bar from inventory to these 2
menus"): both container sides (picker kinds 3/4, WBP_ModernMenu_ContainerInventory_MainContent like the Inventory) get the
Inventory header row: the enchanted/stolen/category filters in the strip, the sort buttons over their columns, the guides
and rules, the controller glyph at the row end and the row status icons under the toggles. Owner run on A0A6D6BA: the side
names were not drawn with the header row ("there are no names", like RMI-073/RMI-079), so each side gets our own title
label, and "the search bar is not in the same position as in the inv": the side bars sit in the title row like the
Inventory's. KB mods/menus-revamped/menus/Container/CHECKLIST.md open items."""
import sys
from pathlib import Path

import source_paths

root = Path(__file__).resolve().parents[1] / "src" / "sorting"
internal = (root / "PickerInternal.h").read_text(encoding="utf-8")
layout = (root / "InventoryHeaders.cpp").read_text(encoding="utf-8")
picker = (root / "Picker.cpp").read_text(encoding="utf-8")
layout_fn = source_paths.function(layout, "bool Picker::LayoutInventoryHeaders(")
filters_fn = source_paths.function(layout, "std::array<LayoutFilter,3> Filters(")
bound_fn = source_paths.function(layout, "void OnInventoryRowBound(")
compensate_fn = source_paths.function(layout, "void Picker::CompensateInventorySlots(")
refresh_fn = source_paths.function(picker, "void RefreshContainerHeaders(")
header_src = (root / "PickerHeader.cpp").read_text(encoding="utf-8")
popup_src = (root / "PickerPopup.cpp").read_text(encoding="utf-8")
own_src = (root / "OwnSort.cpp").read_text(encoding="utf-8")
search_root = Path(__file__).resolve().parents[1] / "src" / "search"
bar_src = (search_root / "SearchBar.cpp").read_text(encoding="utf-8")
bar_rect_fn = source_paths.function(bar_src, "bool ContainerBarRect(")
search_cfg = (search_root / "SearchConfig.h").read_text(encoding="utf-8")
search_cfg_cpp = (search_root / "SearchConfig.cpp").read_text(encoding="utf-8")
defaults_ini = (Path(__file__).resolve().parents[1] / "config" / "MenusRevamped.defaults.ini").read_text(encoding="utf-8")
theme = (Path(__file__).resolve().parents[1] / "src" / "core" / "UiTheme.h").read_text(encoding="utf-8")
theme_skinned = next((l for l in theme.splitlines() if "bool SkinnedKind(" in l), "")
header_line = next((l for l in internal.splitlines() if "bool HeaderLayout()" in l), "")
strip_line = next((l for l in internal.splitlines() if "bool InventoryStrip()" in l), "")

checks = {
    "the Inventory strip kinds are the Inventory and both container sides":
        "kind == 0" in strip_line and "kind == 3" in strip_line and "kind == 4" in strip_line,
    "HeaderLayout covers the Inventory strip and the Magic rows": "InventoryStrip()" in header_line and "kind == 1" in header_line
        and "kind == 6" in header_line,
    "the container sides use the Inventory filter slots (enchanted, stolen, category)": "p.InventoryStrip()" in filters_fn,
    "Magic is named by its kinds, not by 'not the Inventory'": "const bool magic=kind==1 || kind==6;" in layout_fn,
    "container guides get a name per side (one shared canvas: a shared name replaces the other side's guide)":
        "MrContainerHeaderGuide" in layout_fn,
    "the title label stays Inventory only (RMI-073; container names: baked, owner 2026-11-12)": "if (kind==0) PlaceTitleLabel(" in layout_fn,
    "the container bars sit in the side's title row (inv_cont_title)": '"inv_cont_title"' in bar_rect_fn
        and "ty + th * 0.5 - a_h * 0.5" in bar_rect_fn,
    "the container bars end 4 inside the side's header (owner-accepted on C8FB448F)": "hx + hw - kSideBarRightInset - a_w" in bar_rect_fn
        and "constexpr double kSideBarRightInset = 4;" in bar_src,
    "the container bars sit 10 further right (owner 2026-11-12, \"10 px shuld be enough\")": "+ kSideBarOwnerShift" in bar_rect_fn
        and "constexpr double kSideBarOwnerShift = 10;" in bar_src,
    "the container bars take the Inventory bar's style (owner 2026-11-12: dark, not brown)":
        "a_menu == menus::Menu::kContainerYours || a_menu == menus::Menu::kContainerTheirs" in source_paths.function(bar_src, "const uistyle::Search& BarStyle("),
    "no TEMP-DIAG probe is left (C1C findings recorded in the KB)": "TEMP-DIAG C1C" not in bar_src and "TEMP-DIAG C1C" not in layout,
    "the container bars take the Inventory bar's size": "a_w = c.inventoryBar.w;" in bar_rect_fn and "a_h = c.inventoryBar.h;" in bar_rect_fn,
    "the container bar defaults are offsets from the title-row place": "Place yoursBar{ 0, 0, 0, 0 }, theirsBar{ 0, 0, 0, 0 };" in search_cfg
        and "Place takeBar{ 0, 0, 0, 0 };" in search_cfg,
    "the old container keys (above the header, width 825) are not read": "uTitleRowXOffset" in search_cfg_cpp
        and 'place(L"ContainerYours"' not in search_cfg_cpp and 'place(L"ContainerTake"' not in search_cfg_cpp,
    "the shipped INI has the title-row keys, not the old place": "uTitleRowXOffset=0" in defaults_ini and "uXOffset=-48" not in defaults_ini,
    "the Inventory title skin stays Inventory only": "if (kind==0) menuskin::StyleTitle(" in layout_fn,
    "row status icons follow the picker whose side holds the row": "ui::IsWithin(row" in bound_fn and "InventoryStrip()" in bound_fn,
    "the A-Z resize compensation runs for the container sides too": "!InventoryStrip()" in compensate_fn,
    "the container post-event compensates before the paint (RMI-077)": "CompensateInventorySlots()" in refresh_fn,
    "the container sides use the Inventory strip order (panel and controller carousel)":
        "const bool inventoryColumns = InventoryStrip() && OwnSorts();" in header_src,
    "the container sides paint their controls like the Inventory (skinned kinds 3/4)":
        "kind == 3" in theme_skinned and "kind == 4" in theme_skinned,
    "the Magic-only full-white inactive icons stay Magic only":
        own_src.count("!uistyle::InventoryStripKind(kind) && uistyle::SkinnedKind(kind)") == 2,
    "the toggles invert like the Inventory's": popup_src.count("InventoryStrip() && uistyle::SkinFor(mrconfig::Get().stylePreset).invertToggles") == 2,
    "unavailable sorts keep their slot width on the container sides too": "if (InventoryStrip() && OwnSorts() && !offered)" in own_src,
}
fails = [name for name, ok in checks.items() if not ok]
print(f"{len(checks)} checks, {len(fails)} failures")
for name in fails:
    print("FAIL:", name)
sys.exit(1 if fails else 0)
