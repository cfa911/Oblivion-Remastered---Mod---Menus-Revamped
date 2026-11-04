"""IMP-MR-005: the Alchemy Weight and Value sorts read the ingredient's form.

The first runtime (2026-11-04) looked the row's name key minus "LOC_FN_" up as an editor ID and found no ingredient
(the editor-ID map does not hold them), so both sorts fell back to name order. The proven route (Inventory rows, KB
docs/inventory/internals.md section 4) is the name key itself: an item's TESFullName::GetFullName is its LOC_FN_ key.
FormData builds a key -> weight/value index once from every loaded IngredientItem and looks the row's key up there.
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


start = src.find("std::pair<float, int> FormData(")
body = src[start:src.find("\n\t\t}\n", start)]
check(start >= 0, "FormData found")
check("LookupByEditorID" not in src, "no editor-ID lookup (it found no ingredient at run time)")
check("IngredientStats(" in body, "FormData looks the row's name key up in the ingredient index")
index = src[src.find("void AddIngredientForm("):start]   # the index helpers sit just before FormData
check("GetAllForms()" in index and "IngredientItem" in index and "GetFullName(" in index,
      "the index is built from every loaded IngredientItem's full-name key")
check("__try" in index or "Guarded" in index, "the form walk is guarded (SEH)")
print(f"{checks} checks, {failures} failures")
sys.exit(1 if failures else 0)
