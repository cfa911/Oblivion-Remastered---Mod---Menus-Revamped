"""RMI-111 (owner 2026-11-17): the sort bar is framed by exactly two lines, above and below, both the game's own line, in every
menu with our sort bar (Inventory, container/barter sides, Magic spell pages, Active Effects, Spell Purchase); none of our
drawn rules.

- Above: the menu's own HeaderList_Separator (WBP_OriginalImageTile_C, art T_Line_Brown, full list width), 2 units above the
  strip (probe MRSEP, KB probing/snippets/mr-header-separator.md). Magic no longer collapses its own: no run-time hide and no
  layout.json rewrite.
- Below: a copy of that line (the brush of its inner `Image`, so its art and preset colour) at the lower rule's place, with
  the separator's x, width and height.
Earlier (owner 2026-11-13, choice "Native line, full width"): the native line replaced only the Inventory strip's upper rule."""
import json
import re
import sys

import source_paths

fails = []
count = 0


def check(ok, why):
    global count
    count += 1
    if not ok:
        fails.append(why)


ROOT = source_paths.ROOT
headers = (ROOT / "src/sorting/InventoryHeaders.cpp").read_text(encoding="utf-8-sig")
rules = (ROOT / "src/sorting/rules/InventoryHeaderLayout.h").read_text(encoding="utf-8-sig")
layout_fn = source_paths.function(headers, "bool Picker::LayoutInventoryHeaders(")
layout = json.loads((ROOT / "tools/menus/layout.json").read_text(encoding="utf-8"))["layout"]

check(not re.search(r'ui::Visible\(ui::FindInTree\(a_menu,\s*"HeaderList_Separator"\),\s*false\)', headers),
      "no menu hides its HeaderList_Separator at run time")
magic = layout.get("WBP_ModernMenu_MagicMenu", {})
check(not any(r.get("widget", "").startswith("HeaderList_Separator") for r in magic.get("rewrites", [])),
      "the layout pack no longer collapses Magic's HeaderList_Separator")
check("DrawsUpperRule" not in rules + headers, "no menu draws our own upper rule")
check(re.search(r"const bool nativeUpper=i==std::size_t\(inventoryheaders::kGuides-2\);", layout_fn) is not None,
      "the guide loop skips our upper rule in every menu")
check("NativeLine(" in layout_fn and "kGuides-1" in layout_fn, "the lower rule is the native line copy")
native = source_paths.function(headers, "UE::UObject* NativeLine(") if "UE::UObject* NativeLine(" in headers else ""
check('"Image"' in native and '"Brush"' in native and 'L"SetBrush"' in native,
      "the copy takes the brush of the separator's inner Image (its art and preset colour)")
check('"HeaderList_Separator"' in layout_fn, "the lower line is placed from the menu's own separator")
check("TEMP-DIAG" not in headers, "no temporary diagnostics")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
