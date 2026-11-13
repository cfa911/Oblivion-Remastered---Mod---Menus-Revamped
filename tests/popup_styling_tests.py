"""RMI-096 styling round (owner 2026-11-13, "Done, go ahead"): the popup paper much darker (stock grain kept), a blue Laurels
group (Inventory button laurels, popup title flowers, popup leaf border: one Pack Builder role, default accent blue), the
slider silver (bar and handle) and its focus band dark neutral.

Facts (KB docs/reverse-engineering/dark-mode-ui.md "Popup paper", game 1.512.105.0): the popup shader lifts the paper by
about 44 on screen (texture 8..33 showed 49..66), so the paper's light end is parchmentLight x 0.42 (~14); title flowers
T_UI_HelpPattern1 (BC3, Quantity/Sleep-Wait/Negotiate/Help); slider T_UI_SliderBase (both popup sliders) and T_Sliderhandle
(also Character Creation and Settings sliders); the band is the slider's Highlight_Image T_Character_SettingSlidersV2 (white
with alpha, no blueprint tint; also Character Creation)."""
import io
import json
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
from pack_builder import pipeline as pipe  # noqa: E402
import custom_colours as cc  # noqa: E402

BLUE = [66.0, 99.0, 153.0]
rows = {}
for path in (ROOT / "tools/menus").glob("*.json"):
    for row in json.loads(path.read_text(encoding="utf-8")).get("assets", []):
        rows[row["asset"]] = row

WANT = {
    "Art/UI/Common/Ornaments/T_UI_HelpPattern1": ("laurel_tint", ["pattern"]),
    "Art/UI/Common/Sliders/T_UI_SliderBase": ("grey_scale:1.2", []),
    "Art/UI/Common/Sliders/T_Sliderhandle": ("grey_scale:1.2", []),
    "Art/UI/Modern/GameMenuLayer/CharacterCreation/T_Character_SettingSlidersV2": ("flat:listHighlight", ["listHighlight"]),
}
for asset, (recipe, colours) in WANT.items():
    row = rows.get(asset)
    check(row is not None and row["recipe"] == recipe and row["colours"] == colours and row["shared_with"],
          f"{asset}: recipe {recipe}, colours {colours}, shared list named ({row and (row['recipe'], row['colours'])})")

# the Laurels group: pattern (and its [Skin] tint) default accent blue, one role of its own
check(cc.COLOURS.get("pattern") == BLUE, f"pattern (the laurels) defaults to accent blue ({cc.COLOURS.get('pattern')})")
check(re.search(r"Rgb pattern\{\s*66,\s*99,\s*153\s*\}", (ROOT / "src/core/DarkStyle.h").read_text(encoding="utf-8")) is not None,
      "DarkStyle.h has the blue laurel default")
ini = (ROOT / "config/CustomColours.ini").read_text(encoding="utf-8")
check(re.search(r"^pattern\s*=\s*66,\s*99,\s*153\b", ini, re.M) is not None, "the shipped CustomColours.ini has the blue laurels")
laurels = cc.ROLES.get("laurels", {})
check(laurels.get("members") == ["pattern", "Skin.pattern"] and "popup" in laurels.get("label", "").lower(),
      f"a Laurels role groups every laurel, its label names the popups ({laurels})")
check(all("pattern" not in r["members"] for k, r in cc.ROLES.items() if k != "laurels"), "no other role feeds the laurels")
check("laurel" in cc.HELP.get("pattern", "").lower() and "popup" in cc.HELP.get("pattern", "").lower()
      and "border" not in cc.HELP.get("pattern", "").lower() and "border" not in laurels.get("label", "").lower(),
      f"pattern's label names the popup laurels ({cc.HELP.get('pattern')})")

# recipes
try:
    from PIL import Image
    numbers, colours, fn = pipe.recipe("laurel_tint")
    check(colours == ("pattern",), f"laurel_tint reads pattern ({colours})")
    numbers, colours, scale = pipe.recipe("paper_scale:0.42")
    check(colours == ("parchmentLight",), "paper_scale:<factor> reads parchmentLight")
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        stock = work / "T_UI_HelpPattern1.dds"
        image = Image.new("RGBA", (64, 32), (0, 0, 0, 0))
        for x in range(8, 56):
            for y in range(4, 28):
                image.putpixel((x, y), (132, 113, 104, 255) if x < 32 else (66, 56, 52, 255))
        image.save(stock, pixel_format="DXT5")
        alpha = [p[3] for p in Image.open(stock).convert("RGBA").get_flattened_data()]
        (work / "out.dds").write_bytes(fn(stock, {"pattern": BLUE}, work, None))
        out = Image.open(work / "out.dds").convert("RGBA")
        bright, dim = out.getpixel((16, 16)), out.getpixel((48, 16))
        check(all(abs(a - b) <= 10 for a, b in zip(bright[:3], BLUE)), f"the brightest laurel part becomes the laurel blue ({bright})")
        check(dim[2] > dim[0] and dim[2] < bright[2] - 30, f"darker parts stay darker, still blue ({dim})")
        check([p[3] for p in out.get_flattened_data()] == alpha, "the laurel's alpha (shape) is kept")

        head = bytearray(148)
        head[0:4], head[4:8], head[12:16], head[16:20] = b"DDS ", (124).to_bytes(4, "little"), (16).to_bytes(4, "little"), (16).to_bytes(4, "little")
        head[84:88], head[128:132] = b"DX10", (61).to_bytes(4, "little")
        paper = work / "T_Paper_Wrinkled_D.dds"
        paper.write_bytes(bytes(head) + bytes(187 + (i * 55) // 255 for i in range(256)))
        body = scale(paper, {"parchmentLight": [33.0] * 3}, work, None)[148:]
        check(13 <= max(body) <= 15, f"the popup paper's light end is parchmentLight x 0.42 ({max(body)})")
    # popup laurel border (probe 2026-11-13): the shader reads only R as a mask and paints the laurels white; owner "very faint"
    numbers, colours, peak = pipe.recipe("mask_peak:40")
    check(colours == (), "mask_peak reads no colour (the laurel colour is the shader's)")
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        leaf = work / "T_Popup_Flowers_D.dds"
        image = Image.new("RGB", (64, 16), (0, 0, 0))
        for x in range(0, 64, 2):
            for y in range(4, 12):
                image.putpixel((x, y), (148, 146, 148) if x < 32 else (74, 73, 74))
        image.save(leaf, pixel_format="DXT1")
        out = Image.open(io.BytesIO(peak(leaf, {}, work, None))).convert("RGB")
        reds = [out.getpixel((x, 8))[0] for x in range(64)]
        check(36 <= max(reds) <= 44, f"the brightest laurel mask is about 40 (very faint) ({max(reds)})")
        check(out.getpixel((1, 8))[0] <= 3 and out.getpixel((1, 0))[0] <= 3, "the background stays 0 (no laurel there)")
except Exception as error:   # noqa: BLE001
    check(False, f"popup styling recipes run ({type(error).__name__}: {error})")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
