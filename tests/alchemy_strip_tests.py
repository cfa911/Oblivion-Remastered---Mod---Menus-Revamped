"""IMP-MR-006 layout: the active-filter buttons above the Alchemy Ingredients list.

Owner 2026-11-04: buttons over two/three rows in extra parchment space. Owner 2026-11-11: that space is the game's own
parchment (Alchemy_IngredientsListBG) made taller by compact rows, not a separate Alchemy-TopParchment strip image.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

src = source_paths.read("alchemy")
theme = (Path(__file__).resolve().parents[1] / "src/core/UiTheme.h").read_text()
checks = failures = 0


def check(ok, why):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print("FAIL", why)


def z_in(function):
    body = src[src.find(function):]
    m = re.search(r"const int z = (-?\d+);", body)
    return int(m.group(1)) if m else None


back, label = z_in("UE::UObject* MakeBack("), z_in("UE::UObject* MakeLabel(")
check(None not in (back, label) and back < label, "button boxes under the labels")
root = Path(__file__).resolve().parents[1]
check("MakeStrip" not in src and "kStripFile" not in src and "Alchemy-TopParchment" not in src and "g_strip" not in src,
      "no strip image code (owner 2026-11-11: the parchment itself is taller)")
check(not (root / "assets/Alchemy/Alchemy-TopParchment.png").exists(), "the strip image is not shipped")
check("PlaceStrip" not in (root / "src/sorting/rules/AlchemyRules.h").read_text(encoding="utf-8"), "no strip placement rule")
check("ChipsInParchment(pb)" in src and "mrconfig::Get().compactRows" in src,
      "buttons follow the parchment top only with compact rows (else their own line under the title row)")
# owner 2026-11-04: a "Selected filters" title over two rows of slightly smaller buttons
check('"Selected Filters"' in src, "the active-filter area has a Selected Filters title")
check("CopyIngredientsFont(chipTitle, title)" in src, "title uses Ingredients font")
check("line, 0.5f, 6.0f" in src, "button outlines reduced by one from 1.5")
m = re.search(r"19\.0,\s*13\.0,", theme)
check(m is not None and "style.chipFontSize" in src,
      "the button texts are smaller than before (17)")
check("kChipRows = 3" in src, "up to three rows of buttons")
# owner 2026-11-04 (second look): smaller text and boxes, title 4 bigger (21), the block moved up into the strip's top
m = re.search(r"19\.0,\s*13\.0", theme)
check(m is not None and "style.chipFontSize" in src, "button text smaller again (13)")
check("kButtonPadX = 7, kButtonPadY = 2" in src, "smaller button boxes")
check('L"MrAlchemyChipTitle", ink.data(), 23.1)' in src, "title is 10 percent larger (21 -> 23.1)")
check("ChipsInParchment(pb)" in src, "the block sits in the parchment's top space")
# owner 2026-11-04: Clear all stays red; a filter button has a black rim and black text, its x mark red
style = src[src.find("void StyleBack("):]
style = style[:style.find("\n\t\t}\n")]
check("style.buttonLineDark.data()" in style and "style.buttonLine.data()" in style and "a_index == 0" in style,
      "filter buttons get a black rim, Clear all keeps the red one")
check('"' + "\\" + 'xC3' + "\\" + 'x97 " + ChipLabel' not in src, "the x mark is not part of the black name text")
check(re.search(r'MrAlchemyChipX[^;]*style\.chipInk\.data\(\)', src) is not None, "the x mark is its own red label")
# owner 2026-11-04: the x marks stayed on screen after Clear all - every mark had the same object name, so creating one
# replaced the last and the old ones could no longer be hidden. Each created widget needs its own name.
check('L"MrAlchemyChipX",' not in src and re.search(r'L"MrAlchemyChipX" \+ std::to_wstring', src) is not None,
      "each x mark gets its own name")
check(re.search(r"g_chips\.empty\(\) \? style\.chipInk\.data\(\) : style\.sortInk\.data\(\)", src) is not None,
      "Clear all text red, filter names dark")
check('L"MrAlchemyChipTitle", ink.data(), 23.1)' in src and "titleInk[0] * 0.8f, titleInk[1] * 0.8f, titleInk[2] * 0.8f" in src,
      "title ink is 20 percent darker in linear color")
print(f"{checks} checks, {failures} failures")
sys.exit(1 if failures else 0)
