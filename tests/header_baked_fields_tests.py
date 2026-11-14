import hashlib
import json
import sys

from source_paths import ROOT

sys.path.insert(0, str(ROOT / "tools"))
from pack_builder.pipeline import add_field, parse_header

layout = json.loads((ROOT / "tools/menus/layout.json").read_text())["layout"]
cases = (
    ("WBP_ModernMenu_MagicMenu", "HeaderList_Separator Visibility", 62, 1,
     "0402070216020903000000000000404000000000000000400602030300003b4400008040e2feffffbf0000000000000000"),
    ("WBP_OriginalMenu_Inventory_FilterScrollbox", "ScrollBox Clipping", 38, 0, "82022403010400000000"),
)
failures = 0
for asset, widget, field, value, stock in cases:
    original = bytes.fromhex(stock)
    recipe = next((row for row in layout.get(asset, {}).get("rewrites", []) if row["widget"] == widget), None)
    if recipe:
        encoded, = (bytes.fromhex(raw) for index, raw in recipe["add_bytes"] if index == field)
        patched = add_field(original, 0, recipe["field_sizes"], field, encoded)
        old_fields, old_zeroes, old_values = parse_header(original, 0)
        fields, zeroes, values = parse_header(patched, 0)
        at = values + sum(recipe["field_sizes"][i] for i in old_fields if i < field and i not in old_zeroes)
        passed = (hashlib.sha256(original).hexdigest() == recipe["stock_export_sha256"]
                  and fields == sorted(old_fields + [field]) and zeroes == old_zeroes
                  and patched[at] == value and patched[values:at] + patched[at + 1:] == original[old_values:])
    else:
        passed = False
    if not passed:
        print(f"FAIL {widget}")
        failures += 1
print(f"{len(cases)} checks, {failures} failures")
sys.exit(bool(failures))
