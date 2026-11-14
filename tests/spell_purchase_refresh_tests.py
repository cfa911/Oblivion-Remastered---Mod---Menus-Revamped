import sys

import source_paths

source = source_paths.read("own-sort")
republish = source_paths.function(source, "void Republish(Picker&")
purchase = republish.split("static std::uint8_t s_next", 1)[0]
checks = {
    "sort click reads the current full shop list": 'L"GetSpellList"' in purchase,
    "sort click publishes rows without depending on a native sort change":
        "PublishPurchaseRows(" in purchase and "OnSortTypeUpdated" not in purchase,
}
for name, passed in checks.items():
    if not passed:
        print(f"FAIL {name}")
failures = sum(not passed for passed in checks.values())
print(f"{len(checks)} checks, {failures} failures")
sys.exit(bool(failures))
