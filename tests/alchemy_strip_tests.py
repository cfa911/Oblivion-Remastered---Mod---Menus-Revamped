"""IMP-MR-006 layout: the parchment strip above the Alchemy Ingredients panel.

Owner 2026-11-04: the strip goes on the top-most layer (over the menu's own panels, as in the owner's PSD), under our
own sort label and active-filter buttons so they stay visible and clickable.
"""
import re
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


def z_in(function):
    body = src[src.find(function):]
    m = re.search(r"const int z = (-?\d+);", body)
    return int(m.group(1)) if m else None


strip, back, label = z_in("UE::UObject* MakeStrip("), z_in("UE::UObject* MakeBack("), z_in("UE::UObject* MakeLabel(")
check(strip is not None and strip > 0, "strip above the menu's own widgets (Z > 0)")
check(None not in (strip, back, label) and strip < back < label, "strip under the button boxes, boxes under the labels")
# owner 2026-11-04: a "Selected filters" title over two rows of slightly smaller buttons
check('"Selected Filters"' in src, "the active-filter area has a Selected Filters title")
check("CopyIngredientsFont(chipTitle, title)" in src, "title uses Ingredients font")
check("line, 0.5f, 6.0f" in src, "button outlines reduced by one from 1.5")
check("kStripAspect = 263.0 / 1090.0" in src and "kStripDy = -93.7" in src,
      "latest owner PSD mask keeps its 1090x263 size and y=0 placement")
m = re.search(r"kChipText = (\d+)", src)
check(m is not None and int(m.group(1)) < 17 and src.count("kChipInk, kChipText)") >= 2,
      "the button texts are smaller than before (17)")
check("kChipRows = 3" in src, "up to three rows of buttons")
# owner 2026-11-04 (second look): smaller text and boxes, title 4 bigger (21), the block moved up into the strip's top
m = re.search(r"kChipText = (\d+)", src)
check(m is not None and int(m.group(1)) <= 13, "button text smaller again (13)")
check("kButtonPadX = 7, kButtonPadY = 2" in src, "smaller button boxes")
check('L"MrAlchemyChipTitle", ink, 23.1)' in src, "title is 10 percent larger (21 -> 23.1)")
check(re.search(r"chipsY = st\.chipsY \+ kChipsDy", src) is not None and re.search(r"kChipsDy = -\d+", src) is not None,
      "the block moved up")
# owner 2026-11-04: Clear all stays red; a filter button has a black rim and black text, its x mark red
style = src[src.find("void StyleBack("):]
style = style[:style.find("\n\t\t}\n")]
check("kButtonLineDark" in style and "a_index == 0" in style, "filter buttons get a black rim, Clear all keeps the red one")
check('"' + "\\" + 'xC3' + "\\" + 'x97 " + ChipLabel' not in src, "the x mark is not part of the black name text")
check(re.search(r'MrAlchemyChipX[^;]*kChipInk', src) is not None, "the x mark is its own red label")
# owner 2026-11-04: the x marks stayed on screen after Clear all - every mark had the same object name, so creating one
# replaced the last and the old ones could no longer be hidden. Each created widget needs its own name.
check('L"MrAlchemyChipX",' not in src and re.search(r'L"MrAlchemyChipX" \+ std::to_wstring', src) is not None,
      "each x mark gets its own name")
check(re.search(r"g_chips\.empty\(\) \? kChipInk : kSortInk", src) is not None, "Clear all text red, filter names dark")
check('L"MrAlchemyChipTitle", ink, 23.1)' in src and "kTitleInk[0] * 0.8f, kTitleInk[1] * 0.8f, kTitleInk[2] * 0.8f, 1.0f" in src,
      "title ink is 20 percent darker in linear color")
print(f"{checks} checks, {failures} failures")
sys.exit(1 if failures else 0)
