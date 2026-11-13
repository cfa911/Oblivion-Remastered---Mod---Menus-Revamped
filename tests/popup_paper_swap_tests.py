"""RMI-096 paper swap (owner 2026-11-13 "Yes, swap the paper"): the popups stop drawing M_Popup_PaperBackground (its shader adds
~44 grey, paints the laurels white and burns the edges brown) and draw the plain texture T_UI_BackgroundHorizontal instead, the
route Dark Mode UI (Nexus 3317) uses (an import swap in each popup's .uasset; KB dark-mode-ui.md "Popup paper"). That texture is a
complete popup paper (BC3 2048x1024, torn edges in alpha, laurels painted in, unused by stock widgets), baked with popup_paper:
paper near black (parchmentDark..parchmentLight), laurels blue (pattern), alpha kept."""
import json
import os
import struct
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

M = "/Game/UI/Materials/Common/PopupPapers/"
TO = {"to_package": "/Game/Art/UI/Common/Backgrounds/T_UI_BackgroundHorizontal", "to_object": "T_UI_BackgroundHorizontal",
      "to_class": "Texture2D"}


def swap(material, cls):
    return [dict(from_package=M + material, from_object=material, from_class=cls, **TO)]


DIRECT = swap("M_Popup_PaperBackground", "Material")
EXPECTED = {
    "WBP_ModernMenu_Quantity": DIRECT, "WBP_ModernMenu_SleepWait": DIRECT, "WBP_GenericMenu_Specific_SkillIncrease": DIRECT,
    "WBP_Modern_SettingsMenu_TimedRevertPopup": DIRECT, "WBP_Modern_SettingsMenu_ValidationPopup": DIRECT,
    "WBP_Modern_Settings_SaveLoadPopup": DIRECT,
    "WBP_MessageMenu_Specific_Default": swap("MIC_Popup_PaperBackground_Generic", "MaterialInstanceConstant"),
    "WBP_ModernMenu_QuestAdded": swap("MIC_Popup_PaperBackground_Quest", "MaterialInstanceConstant"),
    "WBP_ModernMenu_Negotiate": swap("MIC_Popup_PaperBackground_Quest", "MaterialInstanceConstant"),
    "WBP_Modern_SkillsMenu_SkillsAttributesSelection": swap("MIC_Popup_PaperBackground_Quest", "MaterialInstanceConstant"),
    "WBP_Modern_Onboarding_EULAScreen": swap("MIC_Popup_PaperBackground_EULA", "MaterialInstanceConstant"),
}
data = json.loads((ROOT / "tools/menus/common.json").read_text(encoding="utf-8"))
check(data.get("import_swaps") == EXPECTED, "common.json import_swaps: the 11 popups' paper material -> T_UI_BackgroundHorizontal")
check(getattr(pipe, "IMPORT_SWAPS", None) == EXPECTED, "the pipeline loads the import swaps")
rows = {a["asset"]: a for a in data["assets"]}
paper = rows.get("Art/UI/Common/Backgrounds/T_UI_BackgroundHorizontal")
check(paper is not None and paper["recipe"] == "popup_panel:0.05:0.03" and paper["colours"] == ["parchmentLight"]
      and paper["shared_with"], f"T_UI_BackgroundHorizontal: plain panel, soft vignette + thin border, 5% off the sides and 3% off "
      f"top/bottom (owner 2026-11-13: mock-up D; 15%/8% let the Load Game text and the slider overflow) ({paper})")
for gone in ("Art/UI/Materials/Textures/T_Paper_Wrinkled_D", "UI/Materials/Common/PopupPapers/T_Popup_Flowers_D"):
    check(gone not in rows, f"{gone}: no longer baked (only the swapped-out material drew it)")


# a synthetic UE 5.3 legacy package with the layout of the stock popups (KB dark-mode-ui.md "Popup paper")
def fstr(s):
    b = s.encode("latin-1") + b"\0"
    return struct.pack("<i", len(b)) + b


def package(names, imports, exports_data):
    head = struct.pack("<Iiiiiii", 0x9E2A83C1, -8, 0, 0, 0, 0, 0)
    tail_fields = []
    pkg = fstr("/Game/Test/WBP_Test")

    def build(offs):
        s = head + struct.pack("<i", offs["total"]) + pkg + struct.pack("<I", 0x80000000)
        s += struct.pack("<ii", len(names), offs["names"]) + struct.pack("<ii", 0, offs["soft"]) + struct.pack("<ii", 0, 0)
        s += struct.pack("<ii", len(exports_data), offs["exp"]) + struct.pack("<ii", len(imports), offs["imp"])
        s += struct.pack("<i", offs["dep"]) + struct.pack("<ii", 0, 0) + struct.pack("<ii", 0, 0) + bytes(16) + struct.pack("<i", 0)
        s += (struct.pack("<HHHI", 5, 3, 2, 0) + fstr("")) * 2 + struct.pack("<IiIi", 0, 0, 0, 0)
        s += struct.pack("<i", offs["ar"]) + struct.pack("<q", offs["bulk"]) + struct.pack("<i", 0) + struct.pack("<i", 0)
        s += struct.pack("<ii", 0, offs["pre"]) + struct.pack("<i", 0) + struct.pack("<q", -1) + struct.pack("<i", -1)
        return s

    probe = build(dict(total=0, names=0, soft=0, exp=0, imp=0, dep=0, ar=0, bulk=0, pre=0))
    name_map = b"".join(fstr(n) + b"\0\0\0\0" for n in names)
    o = {"names": len(probe)}
    o["soft"] = o["imp"] = o["names"] + len(name_map)
    imp = b"".join(struct.pack("<qqiqi", cp, cn, outer, on, 0) for cp, cn, outer, on in imports)
    o["exp"] = o["imp"] + len(imp)
    o["dep"] = o["exp"] + 96 * len(exports_data)
    o["ar"] = o["dep"] + 4 * len(exports_data)
    o["pre"] = o["ar"] + 4
    o["total"] = o["pre"]
    serial, exp = o["total"], b""
    for d in exports_data:
        row = bytearray(96)
        struct.pack_into("<qq", row, 28, len(d), serial)
        exp += bytes(row)
        serial += len(d)
    o["bulk"] = serial + 0
    ua = build(o) + name_map + imp + exp + bytes(4 * len(exports_data)) + struct.pack("<i", 0)
    return ua, b"".join(exports_data)


names = ["/Script/Engine", "/Script/CoreUObject", "Package", "Material", "Texture2D",
         "/Game/UI/Materials/Common/PopupPapers/M_Popup_PaperBackground", "M_Popup_PaperBackground", "/Game/Art/X/T_Other", "T_Other"]
idx = {n: i for i, n in enumerate(names)}
imports = [(idx["/Script/CoreUObject"], idx["Package"], 0, idx[names[5]]),
           (idx["/Script/Engine"], idx["Material"], -1, idx["M_Popup_PaperBackground"]),
           (idx["/Script/CoreUObject"], idx["Package"], 0, idx["/Game/Art/X/T_Other"]),
           (idx["/Script/Engine"], idx["Texture2D"], -3, idx["T_Other"])]
ua, ue = package(names, imports, [b"A" * 40, b"B" * 24])
try:
    s = pipe.package_summary(ua)
    check(s["NameCount"] == len(names) and [n[0] for n in s["names"]] == names, "package_summary reads the name map")
    new = pipe.swap_imports(ua, DIRECT)
    t = pipe.package_summary(new)
    got = [n[0] for n in t["names"]]
    check(TO["to_package"] in got and TO["to_object"] in got and "M_Popup_PaperBackground" not in got,
          f"the material's two names become the texture's ({got})")
    delta = len(new) - len(ua)
    check(delta == len(TO["to_package"]) + len(TO["to_object"]) - len(names[5]) - len("M_Popup_PaperBackground"), f"header grows by the name change ({delta})")
    check(t["TotalHeaderSize"] == s["TotalHeaderSize"] + delta and t["ImportOffset"] == s["ImportOffset"] + delta
          and t["ExportOffset"] == s["ExportOffset"] + delta and t["DependsOffset"] == s["DependsOffset"] + delta
          and t["PreloadDependencyOffset"] == s["PreloadDependencyOffset"] + delta
          and t["AssetRegistryDataOffset"] == s["AssetRegistryDataOffset"] + delta
          and t["BulkDataStartOffset"] == s["BulkDataStartOffset"] + delta and t["SoftObjectPathsOffset"] == s["SoftObjectPathsOffset"] + delta,
          "every offset after the name map moves by the change")
    check(t["PayloadTocOffset"] == -1 and t["DataResourceOffset"] == -1, "absent sections stay -1")
    serials = [struct.unpack_from("<qq", new, t["ExportOffset"] + 96 * i + 28) for i in range(2)]
    check(serials == [(40, t["TotalHeaderSize"]), (24, t["TotalHeaderSize"] + 40)], f"export serial offsets follow ({serials})")
    row = struct.unpack_from("<qqiqi", new, t["ImportOffset"] + 32)
    check(got[row[1]] == "Texture2D" and got[row[3]] == "T_UI_BackgroundHorizontal" and row[2] == -1,
          "the material import is now a Texture2D import of T_UI_BackgroundHorizontal, same outer")
    other = struct.unpack_from("<qqiqi", new, t["ImportOffset"] + 96)
    check(got[other[3]] == "T_Other" and got[other[1]] == "Texture2D", "other imports untouched")
    try:
        pipe.swap_imports(new, DIRECT)
        check(False, "a package without the material is refused")
    except pipe.SkipAsset:
        check(True, "")
except Exception as error:   # noqa: BLE001
    check(False, f"swap_imports rewrites the header ({type(error).__name__}: {error})")

# real stock popups, when a local UI extract is available (MR_STOCK_UI = .../OblivionRemastered/Content)
stock = Path(os.environ.get("MR_STOCK_UI", "F:/modbuild/reports/mr-tab-outlines-20261109/ui-all/OblivionRemastered/Content"))
if stock.is_dir():
    for name, swaps in EXPECTED.items():
        try:
            ua = next(stock.rglob(name + ".uasset")).read_bytes()
            new = pipe.swap_imports(ua, swaps)
            t = pipe.package_summary(new)
            got = [n[0] for n in t["names"]]
            check(swaps[0]["to_object"] in got and swaps[0]["from_object"] not in got and t["TotalHeaderSize"] == len(new),
                  f"{name}: stock header swapped, sizes consistent")
        except Exception as error:   # noqa: BLE001
            check(False, f"{name}: stock swap ({type(error).__name__}: {error})")
else:
    print("note: no local UI extract, stock-package checks skipped")

try:
    from PIL import Image
    numbers, colours, fn = pipe.recipe("popup_paper:0.6")
    check(colours == ("parchmentDark", "parchmentLight", "pattern"), f"popup_paper:0.6 reads the paper and laurel colours ({colours})")
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        src = work / "T_UI_BackgroundHorizontal.dds"
        image = Image.new("RGBA", (64, 128), (200, 196, 188, 255))
        for x in range(64):
            image.putpixel((x, 0), (0, 0, 0, 0))
            for y in range(1, 9):   # the top band is darker than the paper (stock); a lighter laurel runs through it
                image.putpixel((x, y), (190, 186, 178, 255) if y in (3, 4) and x % 8 < 4 else (150, 146, 140, 255))
        for x in range(20, 44):   # a lighter patch of paper in the middle (the stock centre is lighter than the sides)
            for y in range(50, 80):
                image.putpixel((x, y), (232, 228, 220, 255))
        image.save(src, pixel_format="DXT5")
        alpha = [p[3] for p in Image.open(src).convert("RGBA").get_flattened_data()]
        v = {"parchmentDark": [8.0] * 3, "parchmentLight": [33.0] * 3, "pattern": [66.0, 99.0, 153.0]}
        out = Image.open(__import__("io").BytesIO(fn(src, v, work, None))).convert("RGBA")
        body, leaf, patch = out.getpixel((10, 100)), out.getpixel((1, 3)), out.getpixel((32, 65))
        check(max(body[:3]) <= 40 and max(body[:3]) - min(body[:3]) <= 6, f"the paper becomes near black, neutral ({body})")
        check(max(patch[:3]) <= 40 and max(patch[:3]) - min(patch[:3]) <= 6, f"lighter paper in the middle stays neutral, no blue ({patch})")
        check(leaf[2] > leaf[0] + 20 and leaf[2] > body[2] + 25, f"popup_paper:0.6: the laurels in the band become blue ({leaf})")
        numbers, colours0, fn0 = pipe.recipe("popup_paper:0")
        check(colours0 == ("parchmentDark", "parchmentLight"), f"popup_paper:0 reads only the paper colours ({colours0})")
        out0 = Image.open(__import__("io").BytesIO(fn0(src, v, work, None))).convert("RGBA")
        leaf0, gap0 = out0.getpixel((1, 3)), out0.getpixel((5, 3))
        check(max(abs(a - b) for a, b in zip(leaf0[:3], gap0[:3])) <= 3 and max(leaf0[:3]) - min(leaf0[:3]) <= 6,
              f"popup_paper:0: the laurels vanish into the paper around them ({leaf0} vs {gap0})")
        check([p[3] for p in out.get_flattened_data()] == alpha, "the torn edges (alpha) are kept")
except Exception as error:   # noqa: BLE001
    check(False, f"popup_paper bakes the paper ({type(error).__name__}: {error})")

# popup_panel:<side>:<top>: plain paper in parchmentLight, edges fading to 16/33 of it, a 1-screen-pixel border at 64/33 inset
# ~13 screen pixels, the whole panel (stock torn-edge alpha included) shrunk into the centre, transparent around it
try:
    from PIL import Image
    numbers, colours, fn = pipe.recipe("popup_panel:0.05:0.03")
    check(colours == ("parchmentLight",), f"popup_panel reads parchmentLight ({colours})")
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        src = work / "T_UI_BackgroundHorizontal.dds"
        Image.new("RGBA", (2048, 1024), (214, 210, 202, 255)).save(src, pixel_format="DXT5")   # the stock size
        data = fn(src, {"parchmentLight": [33.0] * 3}, work, None)
        check(len(data) == len(src.read_bytes()), "same texture size and mips")
        out = Image.open(__import__("io").BytesIO(data)).convert("RGBA")
        centre, outside_x, outside_y = out.getpixel((1024, 512)), out.getpixel((50, 512)), out.getpixel((1024, 15))
        check(abs(centre[0] - 33) <= 3 and max(centre[:3]) - min(centre[:3]) <= 3 and centre[3] == 255, f"centre is the paper colour ({centre})")
        check(outside_x[3] <= 8 and outside_y[3] <= 8, f"5% off the sides and 3% off top/bottom are transparent ({outside_x}, {outside_y})")
        left = 0.05 * 2048
        near_edge = out.getpixel((round(left) + 12, 512))
        check(near_edge[3] == 255 and near_edge[0] < centre[0] - 6, f"the panel's edge is darker than its centre (vignette) ({near_edge})")
        line = max(out.getpixel((x, 512))[0] for x in range(round(left) + 14, round(left) + 40))
        check(line >= 50, f"a lighter border line runs inside the edge ({line})")
        fade = [out.getpixel((x, 512)) for x in range(round(left) + 40, round(left) + 260)]
        check(all(p[0] == p[2] and abs(p[1] - p[0]) <= 1 for p in fade), f"the vignette is neutral grey, no green cast ({fade[::40]})")
        levels = sorted({p[0] for p in fade})
        check(len(levels) >= 5 and max(b - a for a, b in zip(levels, levels[1:])) <= 4,
              f"the vignette fades in fine steps (thirds of the 5-bit grey ladder), not 2-3 bands ({levels})")
        # owner 2026-11-13 chose option c: neutral blocks + a 4x4 ordered dither of one palette third (no random grain)
        row = [out.getpixel((x, 600))[0] for x in range(round(left) + 100, round(left) + 108)]
        check(row[:4] == row[4:8], f"the dither is a fixed 4x4 pattern (repeats every 4 pixels), not random grain ({row})")
except Exception as error:   # noqa: BLE001
    check(False, f"popup_panel bakes the framed panel ({type(error).__name__}: {error})")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
