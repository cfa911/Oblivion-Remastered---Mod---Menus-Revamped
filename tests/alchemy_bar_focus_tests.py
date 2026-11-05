"""RMI-039: controller LB (Selected Filters focus) and RB (Sort icon focus) hid the Alchemy ingredient search bar, because the
bar was drawn only while the search was *available* for input. The bar stays visible during that focus; input stays blocked.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

root = Path(__file__).resolve().parents[1]
alch = source_paths.read("alchemy")
bar = (root / "src/search/SearchBar.cpp").read_text(encoding="utf-8-sig")
header = (root / "src/alchemy/AlchemyMenu.h").read_text(encoding="utf-8-sig")
checks = failures = 0


def check(ok, why):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print("FAIL", why)


available = source_paths.function(alch, "bool SearchAvailable()")
check("g_padFocus.load() < 0" in available and "g_chipFocus.load() < 0" in available,
      "typing into the bar stays blocked while LB/RB focus is on chips or sort icons")
shown = source_paths.function(alch, "bool SearchShown()") if "bool SearchShown()" in alch else ""
check(shown != "" and "bool SearchShown()" in header, "a separate SearchShown decides whether the bar is drawn")
check("g_padFocus" not in shown and "g_chipFocus" not in shown, "LB/RB focus does not hide the bar")
check("g_filtersVisible" in shown and "g_nameTyping" in shown, "the bar still hides behind the Filters panel and while naming")
tick = source_paths.function(bar, "void BarTick(")
check("alchemy::SearchShown()" in tick, "BarTick uses SearchShown for the Alchemy ingredient bar")
print(f"{checks} checks, {failures} failures")
sys.exit(1 if failures else 0)
