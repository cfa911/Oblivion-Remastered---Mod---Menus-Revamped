"""IMP-MR-005 (owner 2026-11-04): the Alchemy sorts work like the Inventory's native sort header.

- Native header icons, loaded from the game at run time (KB docs/inventory/internals.md "Native sort header icon
  textures"): A-Z, Count (placeholder: the spell-cost lightning), Weight, Value; no game texture shipped as a file.
- Click an icon = its sort; click it again = reversed; the active icon is highlighted with a direction arrow.
- Ingredient rows get Weight and Value columns (a dash when unknown), written from the tick, not inside the game's
  own row call (RMI-022/RMI-024 lesson).
- No "Sort:" label, no S key, no L3; controller: Y enters the icons, left/right moves, A applies/reverses, B leaves.
- The chosen sort is kept between openings (only the first opening reads the INI default).
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

src = source_paths.read("alchemy")
checks = failures = 0


def check(ok, why):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print("FAIL", why)


def body(signature):
    start = src.find(signature)
    if start < 0:
        return ""
    return src[start:src.find("\n\t\t}\n", start)]


for tex in ("T_Property_1_sorting", "T_Property_1_Weight", "T_Property_1_MoneyV2", "T_Property_1_cost",
            "T_Filter_Icon_Background_Small", "T_ArrowStat_indicationUp", "T_ArrowStat_indicationDown"):
    check(f"/Game/Art/UI/" in src and tex in src, f"native texture {tex} is used")
check("ui::Load(" in src, "native textures are loaded from the game (ui::Load), not shipped")
check('"Sort: "' not in src and "MrAlchemySort\"" not in src, "no Sort: label")
check("a_key == 'S'" not in src and "kLeftThumb" not in src, "no S key and no L3")
check("NextSort(" not in src, "no cycling sort")
check("Click(" in src and "g_sortState" in src, "a header click goes through the Click rule (sort / reverse)")
check("MrAlchemyWeight" in src and "MrAlchemyValue" in src, "rows get Weight and Value column labels")
check("FormatWeight(" in src and "FormatValue(" in src, "column text from the shared formatting rules")
entry = body("void OnEntryEvent(")
check("DecorateIngredientRow" not in entry, "ingredient columns are not written inside the game's row call")
tick = src[src.find("\tvoid Tick()"):src.find("\tbool Message(")]
check("DecorateIngredientRows(" in tick, "the tick writes the ingredient columns")
check("g_sortState = { SortFromIni(" not in tick or "g_sortRead" in tick, "the INI default is read once, the sort is kept between openings")
pad = src[src.find("\tvoid Controller("):]
check("kPadRB" in pad and "kPadA" in pad and "kPadB" in pad and "NavStep(" in src, "RB enters sort icons, A applies, B leaves")
check("kPadX" not in pad and "kPadY" not in pad, "native X Create and Y tap/hold pass through without interception")
check("OpenFilters(" not in src and "g_clearAllRequest" not in src and "g_openFiltersRequest" not in src, "no duplicate native Filters or clear-all requests")
check("RemoveEveryFilter(" in src, "custom chip Clear all retains its existing removal path")
# styling batch (owner 2026-11-04): header line between the title and the rows, white active background, centred numbers
check("HeaderX(" in src and "SetPadding" in src, "icons on their own line over the columns; the list moves down for it")
check("kActiveTint[4] = { 0.55f, 0.35f, 0.20f" in src, "active sort is brown; hover unchanged")
check("ColumnDy(" in src, "column numbers are centred on the row's name")
check("kGapTrimPx" in src and "kIconLiftPx" in src and "kLeftShiftPx" in src, "owner pixel adjustments 2026-11-04: gap -4, icon line +3 up, left icons +4 right")
# second batch (owner 2026-11-04): 3 px more bottom margin, left icons 10 px more right, Inventory-style separators
check("kIconLiftPx = 6" in src and "kLeftShiftPx = 17" in src, "icon line 6 px above the rows, lightning/A-Z 17 px right in total")
check("T_Line_V" in src and "MrAlchemySortSep" in src, "separators lightning|A-Z and weight|value use the Inventory's column line")
check("MrAlchemySortRule" in src and "SetRenderTransformAngle" in src, "a horizontal line under the icons: the column line turned 90 degrees")
# controller hints (owner 2026-11-04, KB game-internals "Alchemy footer"): on a pad with the Filters panel closed the
# bottom bar shows Hold Y Selected Filters, Y Sort, X Filters, Hold X Clear All Filters; the Filters button shows X
check("GetCurrentInputType" in src, "pad or keyboard from the game's CommonInputSubsystem")
check('"BA_Box"' in src and "MrAlchemyHint" in src and "Text_ActionName" in src, "own hints in the footer's BA_Box, styled from a native entry's label")
check("T_Icon_Xbox_Y" in src, "native Y glyph used for Filters hints")
for key in ("LB", "RB", "DPad_Right"):
    check(f"T_Icon_Xbox_{key}.T_Icon_Xbox_{key}" in src, f"native {key} glyph is loaded from the game")
check("keyText" not in src and "MrAlchemyHintDirection" not in src, "footer uses glyph images without text-key substitutes")
# owner screenshot on b2d70c38: IconRimBrush drew a white box; hints touched each other; Filters button still Y
check("IconRimBrush" not in src and "ProgressMaterialBrush" in src and "BrushResource(" in body("UE::UObject* MakeHint("),
      "hold ring from the progress brush, used only when it has a picture (IconRimBrush is a plain white box)")
check("kHintSpacing" in body("UE::UObject* MakeHint("), "own hints are spaced from their neighbours")
check("LayoutFiltersGlyph(" not in src, "native Filters Y glyph needs no override")
for label in ("Selected Filters", "Sort", "Filters", "Clear All Filters"):
    check(f'"{label}"' in src, f"hint label {label}")
check("BoundInputAction" in src and "ClearAll" in src, "the native hold-Y entry is found by its input action, not its (localised) text")
check("MrAlchemyFiltersGlyph" not in src, "no custom Filters glyph is created")
# owner screenshot on 3332239a: hold-Y entry not relabelled, own glyphs small and raised, Filters button still Y
check("HoldTreshold" in src, "the native hold entry is found by its hold time (its input action name has no 'ClearAll')")
make = body("UE::UObject* MakeHint(")
check(make.count("FillSlot(") >= 2, "glyph and rim fill their box (overlay slots start top-left at the brush size)")
check("FindByClass(" not in src, "unused Filters glyph tree walker removed")
# owner screenshots on db35721d: on the second opening a second X glyph appeared and the first stayed at the old place
glyph_fn = ""
check("g_filtersGlyph" not in src, "no obsolete Filters glyph handle")
close = src[src.find("g_dyDone.clear();"):]
close = close[:close.find("\n\t\t\t}")]
check("g_filtersGlyph = {}" not in close, "closing the menu keeps the glyph's handle (no orphan, no duplicate)")
# owner order 2026-11-04: A Add to Potion, X Filters, X(hold) Clear All Filters, Y Sort, Y(hold) Selected Filters, B Back
hints = src[src.find("constexpr Hint kHints[]"):]
hints = hints[:hints.find("};")]
order = [hints.find(x) for x in ("kTexPadA", '"Filters"', '"Clear All Filters"', '"Sort"', '"Selected Filters"', "kTexPadB")]
check(all(o >= 0 for o in order) and order == sorted(order), "six hints in the owner's order, B Back last")
footer = body("void LayoutFooter(")
# owner screenshot on fa3e9a32: restoring the native entries' visibility showed keyboard-hidden ones (hold-Y Clear All
# Filters, Add to Potion) on the keyboard bar - the game's per-entry visibility must not be touched
check("SetVisibility" not in footer and "SetRenderOpacity" in footer and "MrAlchemyBar" in footer,
      "on a pad our own bar replaces BA_Box by opacity; the game's entries keep their own visibility")
# RMI-030: LB enters the Selected Filters buttons
pad2 = src[src.find("\tvoid Controller("):]
check("kPadLB" in pad2 and "g_chipFocus" in pad2 and "ClampFocus(" in src and "HoldTracker" not in src, "LB enters Selected Filters without custom hold tracking")
print(f"{checks} checks, {failures} failures")
sys.exit(1 if failures else 0)
