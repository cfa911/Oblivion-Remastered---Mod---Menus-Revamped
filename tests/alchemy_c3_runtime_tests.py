"""Alchemy Step C.3 round 2 (owner 2026-11-14, annotated screenshot; estimate c3-alchemy-round2-preview.png approved with
"just the clear all button, should have a red outline"; KB Alchemy CHECKLIST C.3-4..8). Our own Alchemy widgets in the
Custom preset take four run-time colours that the Pack Builder shows on its Runtime tab ([Skin], linear, no pack Build):
- alchemyHeading: the "Selected Filters" heading (default the menu-title blue; C.3-13: #4f7cb0 = 79, 124, 176);
- alchemyBox: the filter chip fill and the Filters "Search effects" box fill (default grey 40);
- alchemyBoxLine: their outline (default the softer accent blue 66, 99, 153);
- alchemyClearLine: the Clear all chip outline (default the stolen-mark red 210, 99, 91).
Classic and Light keep their own palette."""
import re
import sys
import tempfile
from pathlib import Path

import source_paths

fails = []
count = 0


def check(ok, why):
    global count
    count += 1
    if not ok:
        fails.append(why)


ROOT = source_paths.ROOT
sys.path.insert(0, str(ROOT / "tools"))
import custom_colours as cc  # noqa: E402


def lin(v):
    v /= 255.0
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def close(a, b):
    return isinstance(a, list) and len(a) == 4 and all(abs(a[i] - b[i]) < 0.0015 for i in range(4))


WANT = {"alchemyHeading": (79, 124, 176), "alchemyBox": (40, 40, 40), "alchemyBoxLine": (66, 99, 153),
        "alchemyClearLine": (210, 99, 91)}
for name, rgb in WANT.items():
    check(close(cc.SKIN.get(name), [lin(c) for c in rgb] + [1.0]), f"{name}: [Skin] default = linear sRGB {rgb}")
    check(bool(cc.SKIN_HELP.get(name)) and "Alchemy" in cc.SKIN_HELP.get(name, ""), f"{name}: a builder label naming Alchemy")
    check(name in cc.RUNTIME_SKIN, f"{name}: on the Runtime tab")
    check(name not in cc.BAKED_SKIN and name not in cc.RETIRED_SKIN, f"{name}: run time, not baked or retired")

dark = (ROOT / "src/core/DarkStyle.h").read_text(encoding="utf-8-sig")
tints = dark[dark.find("struct SkinTints"):dark.find("} skin;")]
check(all(f"std::array<float, 4> {n}" in tints for n in WANT), "DarkStyle.h SkinTints names the four Alchemy run-time colours")
ccs = (ROOT / "src/core/rules/CustomColours.h").read_text(encoding="utf-8-sig")
check(all(f"MR_CC_TINT({n}," in ccs for n in WANT), "CustomColours.h reads the four [Skin] entries")
order = re.findall(r"MR_CC_(?:TINT|LIN)\((\w+),", ccs[ccs.find("kEntries"):])   # the entries, not the #defines
check(order == list(cc.SKIN), f"CustomColours.h [Skin] order = the default-file order ({order[-5:]})")

src = (ROOT / "src/alchemy/AlchemyLayout.cpp").read_text(encoding="utf-8-sig")
back = src[src.find("void StyleBack("):]
back = back[:back.find("\n\t\t}\n")]
check("alchemyBox" in back and "alchemyBoxLine" in back and "alchemyClearLine" in back and "Recolors(" in back,
      "the chips take the run-time box colours in Custom (Clear all: the red outline)")
check("buttonLine" in back and "buttonFill" in back, "Classic and Light chips keep their palette")
title = src[src.find("auto* chipTitle = reflect::Get(g_chipTitle);"):]
title = title[:title.find("if (chipTitle) {", 60)]
check("alchemyHeading" in title, "the Selected Filters heading takes alchemyHeading in Custom")
bar = (ROOT / "src/search/SearchBar.cpp").read_text(encoding="utf-8-sig")
style = bar[bar.find("const uistyle::Search& BarStyle("):]
style = style[:style.find("\n\t\t}\n")]
check("kAlchemyFilters" in style and "alchemyBox" in style and "alchemyBoxLine" in style,
      "the Filters Search effects box takes the run-time box colours in Custom")

# the builder: Runtime tab lists them; a change needs no pack Build
from pack_builder.model import ColourModel  # noqa: E402
import json  # noqa: E402
with tempfile.TemporaryDirectory() as tmp:
    m = ColourModel(Path(tmp) / "none.ini")
    runtime = dict(m.pages()).get("Runtime", [])
    names = [e.name for _, entries in runtime for e in entries]
    check(all(n in names for n in WANT), f"the Runtime tab shows the four Alchemy colours ({names})")
    check(any("Alchemy" in heading and all(e.name in WANT for e in entries) for heading, entries in runtime),
          "the four sit in their own Alchemy group on the Runtime tab")
    manifest = json.loads((ROOT / "assets/CustomPack/MRCustom_P.json").read_text(encoding="utf-8"))
    try:
        m.set_text("Skin", "alchemyBox", "0.1, 0.1, 0.1, 1")
        m.set_text("Skin", "alchemyClearLine", "1, 0, 0, 1")
        check(m.needs_build(manifest["style"]) == [], "an Alchemy run-time colour change needs no Build")
    except KeyError as e:
        check(False, f"the builder knows the Alchemy run-time colours ({e})")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
