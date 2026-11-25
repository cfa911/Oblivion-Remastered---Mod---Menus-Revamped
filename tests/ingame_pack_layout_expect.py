"""Writes tests/ingame_pack_layout_expect.inc: the Python package rewrite's results on small synthetic packages (property
headers, add_field, replace_export, rewrite_package, package_summary/swap_imports, bp_float, masked_sha256, [Sizes]), the
oracle for tests/ingame_pack_layout_tests.cpp (Phase 3d).
  python tests/ingame_pack_layout_expect.py > tests/ingame_pack_layout_expect.inc
Each case is a (key, value) pair of strings: inputs are hex, results hex or the SkipAsset message ("!" + message).
"""
import json, random, struct, sys, tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import custom_colours as cc  # noqa: E402
from pack_builder import pipeline as p  # noqa: E402

out = []


def emit(key, value):
    out.append((key, value))


def attempt(fn):
    try:
        return fn(), None
    except p.SkipAsset as error:
        return None, "!" + str(error)


# ---- property headers ----
rnd = random.Random(11)
header_cases = [([0, 2], []), ([2, 3], [2, 3]), ([], []), ([0], [0]), ([130], []), (list(range(0, 140)), [5, 6, 139]),
                ([1, 300, 301, 302], [300]), ([2, 39], [2]), (list(range(0, 20)), list(range(0, 20, 2)))]
for _ in range(25):
    fields = sorted(rnd.sample(range(0, 200), rnd.randrange(1, 40)))
    zeroes = [f for f in fields if rnd.random() < 0.3]
    header_cases.append((fields, zeroes))
for i, (fields, zeroes) in enumerate(header_cases):
    h = p.build_header(fields, zeroes)
    emit(f"hdr{i}.fields", ",".join(map(str, fields)))
    emit(f"hdr{i}.zeroes", ",".join(map(str, zeroes)))
    emit(f"hdr{i}.bytes", h.hex())
    pf, pz, end = p.parse_header(b"\xaa\xbb" + h + b"\x01\x02", 2)
    emit(f"hdr{i}.parsed", ",".join(map(str, pf)) + "|" + ",".join(map(str, sorted(pz))) + "|" + str(end))
emit("hdr.count", str(len(header_cases)))

# ---- add_field (KB techniques/unversioned-package-rewrite.md: the two cases, plus a refusal) ----
add_cases = [
    ("80 0b 06 00 02 01 03 00 00 20 41 00 00 20 41 2e 00 00 00 00", 3, [4, 4, 4, 4], 3, struct.pack("<f", 10.0)),   # skipped
    ("82 05 03 00 00 00 00", 0, [4, 4, 4, 4], 2, struct.pack("<f", -5.0)),                                       # zero mask
    ("00 02 01 03 00 00 20 41 00 00 20 41", 0, [4, 4, 4, 4], 2, struct.pack("<f", 1.0)),                         # stored
    ("01 05 aa bb cc 11", 0, [3, 1, 1, 1, 1, 1], 0, bytes.fromhex("010301")),                                     # first field
]
for i, (export, pos, sizes, field, value) in enumerate(add_cases):
    data = bytes.fromhex(export.replace(" ", ""))
    emit(f"add{i}.in", data.hex())
    emit(f"add{i}.pos", str(pos))
    emit(f"add{i}.sizes", ",".join(map(str, sizes)))
    emit(f"add{i}.field", str(field))
    emit(f"add{i}.value", value.hex())
    r, err = attempt(lambda: p.add_field(data, pos, dict(enumerate(sizes)), field, value))
    emit(f"add{i}.out", err or r.hex())
emit("add.count", str(len(add_cases)))

# ---- a synthetic package whose export table tiles the .uexp (replace_export, rewrite_package) ----
TAG = struct.pack("<I", 0x9E2A83C1)


def tiling(exports, base=400, bulk_at=48, rows_at=200):
    uexp = b"".join(exports) + TAG
    ua = bytearray(base)
    struct.pack_into("<q", ua, bulk_at, base + len(uexp) - 4)
    off = base
    for i, e in enumerate(exports):
        struct.pack_into("<qq", ua, rows_at + 40 * i, len(e), off)
        off += len(e)
    return bytes(ua), uexp


e0 = bytes.fromhex("80 0b 06 00 02 01 03".replace(" ", "")) + struct.pack("<ff", 10.0, 10.0) + bytes(4)
e1 = bytes.fromhex("8205 03".replace(" ", "")) + bytes(4)
e2 = bytes.fromhex("0103") + bytes(range(3, 30)) + bytes(4)
ua, ux = tiling([e0, e1, e2])
emit("tile.uasset", ua.hex())
emit("tile.uexp", ux.hex())
new_e1 = e1[:1] + b"\x77" * 9 + e1[1:]
for i, (off, size, new) in enumerate([(400 + len(e0), len(e1), new_e1), (400, len(e0), e0[:-2]), (400 + len(e0) + len(e1), len(e2), e2 + b"xyz"),
                                      (400 + 3, len(e1), new_e1), (400 + 2000, 5, b"")]):
    r, err = attempt(lambda: p.replace_export(ua, ux, off, size, new))
    emit(f"rep{i}.off", str(off))
    emit(f"rep{i}.size", str(size))
    emit(f"rep{i}.new", new.hex())
    emit(f"rep{i}.out", err or (r[0].hex() + "|" + r[1].hex()))
emit("rep.count", "5")
bad = bytearray(ua)
struct.pack_into("<q", bad, 64, 400 + len(ux) - 4)   # BulkDataStartOffset twice
r, err = attempt(lambda: p.replace_export(bytes(bad), ux, 400, len(e0), e0))
emit("repbulk.uasset", bytes(bad).hex())
emit("repbulk.out", err)

# rewrite_package: the KB's two cases in exports e0 and e1, later export first
rewrites = [
    {"widget": "OverlaySlot_0", "export_start": 0, "export_size": len(e0), "header_pos": 3, "stock_fields": [0, 2],
     "stock_zeroes": [], "add": [[3, -75.0]], "field_sizes": [4, 4, 4, 4], "stock_export_sha256": p.masked_sha256(e0, [])},
    {"widget": "CanvasPanelSlot_0", "export_start": len(e0), "export_size": len(e1), "header_pos": 0, "stock_fields": [2, 3],
     "stock_zeroes": [2, 3], "add": [[2, -5.0], [3, -81.0]], "field_sizes": [4, 4, 4, 4],
     "stock_export_sha256": p.masked_sha256(e1, [])},
    {"widget": "Bytes", "export_start": len(e0) + len(e1), "export_size": len(e2), "header_pos": 0, "stock_fields": [1],
     "stock_zeroes": [], "add_bytes": [[0, "a1b2c3"]], "field_sizes": [3, 1, 1], "sha_mask": [[2, 5], [30, 10]],
     "stock_export_sha256": p.masked_sha256(e2, [[2, 5], [30, 10]])},
]
emit("rw.json", json.dumps(rewrites))
r, err = attempt(lambda: p.rewrite_package(ua, ux, rewrites))
emit("rw.out", err or (r[0].hex() + "|" + r[1].hex()))
wrong = [dict(rewrites[0], stock_export_sha256="00" * 32)]
emit("rwsha.json", json.dumps(wrong))
emit("rwsha.out", attempt(lambda: p.rewrite_package(ua, ux, wrong))[1])
hdr_bad = [dict(rewrites[1], stock_zeroes=[3])]
emit("rwhdr.json", json.dumps(hdr_bad))
emit("rwhdr.out", attempt(lambda: p.rewrite_package(ua, ux, hdr_bad))[1])
stored = [dict(rewrites[0], add=[[2, 1.0]])]
emit("rwstored.json", json.dumps(stored))
emit("rwstored.out", attempt(lambda: p.rewrite_package(ua, ux, stored))[1])
for i, mask in enumerate([[], [[0, 3]], [[5, 100]], [[2, 1], [40, 2]]]):
    emit(f"msha{i}.mask", json.dumps(mask))
    emit(f"msha{i}.out", p.masked_sha256(e2, mask))
emit("msha.count", "4")
emit("msha.data", e2.hex())


# ---- a synthetic legacy package summary (package_summary, swap_imports) ----
def fstr(s):
    return struct.pack("<i", len(s) + 1) + s.encode("latin-1") + b"\0"


def make_package(names, imports, exports, tail=b"\x05" * 12):
    """names [(str, h1, h2)], imports [(class_package, class_name, outer, object_name)] as name indexes, exports [object
    name index]: a summary like retoc's to-legacy output (offset fields after the name map, -1/0 for the absent ones)."""
    def summary(name_offset, total, imp, exp, depends, extra):
        s = struct.pack("<Iiiiiii", 0x9E2A83C1, -8, 864, 522, 1012, 0, 0)
        s += struct.pack("<i", total) + fstr("/Game/UI/WBP_Test") + struct.pack("<I", 0x80000000)
        s += struct.pack("<ii", len(names), name_offset)
        s += struct.pack("<ii", 0, extra) + struct.pack("<ii", 0, 0)   # soft object paths (moved), gatherable text (0)
        s += struct.pack("<ii", len(exports), exp) + struct.pack("<ii", len(imports), imp)
        s += struct.pack("<i", depends) + struct.pack("<ii", 0, extra) + struct.pack("<ii", 0, -1)
        s += bytes(16) + struct.pack("<i", 1) + struct.pack("<ii", 3, 7)
        for _ in range(2):
            s += struct.pack("<HHHI", 5, 3, 2, 0) + fstr("++UE5+Release-5.3")
        s += struct.pack("<IiIi", 0, 0, 0x4D4F4420, 0)
        s += struct.pack("<iqi", extra + 4, total + 100, 0) + struct.pack("<i", 1) + struct.pack("<i", 9)
        s += struct.pack("<ii", 0, extra + 8) + struct.pack("<iqi", 2, -1, -1)
        return s
    head_len = len(summary(0, 0, 0, 0, 0, 0))
    name_map = b"".join(fstr(n) + struct.pack("<HH", h1, h2) for n, h1, h2 in names)
    imp = head_len + len(name_map)
    exp = imp + 32 * len(imports)
    depends = exp + 96 * len(exports)
    extra = depends + 4 * len(exports)
    total = extra + len(tail)
    body = b"".join(struct.pack("<qqiqi", cp, cn, outer, on, 0) for cp, cn, outer, on in imports)
    for i, n in enumerate(exports):
        row = bytearray(96)
        struct.pack_into("<iiii", row, 0, -2, 0, 0, 0)
        struct.pack_into("<q", row, 16, n)
        struct.pack_into("<qq", row, 28, 10 + i, total + 10 * i)
        body += bytes(row)
    out = summary(head_len, total, imp, exp, depends, extra) + name_map + body + bytes(4 * len(exports)) + tail
    assert len(out) == total
    return out


NAMES = [("/Script/Engine", 1, 2), ("/Script/CoreUObject", 3, 4), ("Package", 5, 6), ("Material", 7, 8), ("Texture2D", 9, 10),
         ("/Game/UI/Materials/M_Paper", 11, 12), ("M_Paper", 13, 14), ("WBP_Test", 15, 16), ("/Game/UI/Other", 0, 0), ("Other", 0, 0)]
IMPORTS = [(1, 2, 0, 5), (0, 3, -1, 6), (1, 2, 0, 8), (0, 4, -3, 9)]
pkg = make_package(NAMES, IMPORTS, [7])
emit("pkg.in", pkg.hex())
s = p.package_summary(pkg)
emit("pkg.summary", ",".join(f"{k}={s[k]}@{s['@' + k]}" for k, _ in p.OFFSET_FIELDS) + f",names_end={s['names_end']},end={s['end']}")
SWAP = {"from_package": "/Game/UI/Materials/M_Paper", "from_object": "M_Paper", "from_class": "Material",
        "to_package": "/Game/Art/UI/Common/Backgrounds/T_UI_BackgroundHorizontal", "to_object": "T_UI_BackgroundHorizontal",
        "to_class": "Texture2D"}
SWAP2 = {"from_package": "/Game/UI/Other", "from_object": "Other", "from_class": "Texture2D", "to_package": "/Game/X",
         "to_object": "X", "to_class": "Material"}
swap_cases = [
    ("ok", pkg, [SWAP]),
    ("two", pkg, [SWAP, SWAP2]),
    ("named", pkg, [dict(SWAP, to_object="Package")]),
    ("missing", pkg, [dict(SWAP, from_object="M_Nothing")]),
    ("class", pkg, [dict(SWAP, from_class="Texture2D")]),
    ("users", make_package(NAMES, IMPORTS, [7, 6]), [SWAP]),
    ("engine", make_package(NAMES, [(1, 2, 0, 5), (1, 3, -1, 6)], [7]), [SWAP]),
    ("versioned", pkg[:24] + struct.pack("<i", 1) + pkg[28:], [SWAP]),
]
for i, (label, data, swaps) in enumerate(swap_cases):
    emit(f"swap{i}.label", label)
    emit(f"swap{i}.in", data.hex())
    emit(f"swap{i}.json", json.dumps(swaps))
    r, err = attempt(lambda: p.swap_imports(data, swaps))
    emit(f"swap{i}.out", err or r.hex())
emit("swap.count", str(len(swap_cases)))

# ---- bp_float: floats, a double, a byte, and the refusals ----
uexp = bytearray(64)
struct.pack_into("<f", uexp, 4, 36.0)
struct.pack_into("<f", uexp, 8, 32.0)
struct.pack_into("<d", uexp, 16, 11.0)
struct.pack_into("<f", uexp, 28, 0.1)
uexp[40] = 0
row_ok = {"asset": "UI/X/WBP_Test", "floats": [
    {"widget": "Row", "offset": 4, "stock": 36, "size": "magicRowHeight"},
    {"widget": "Icon", "offset": 8, "stock": 32.0, "size": "magicIconSize"},
    {"widget": "Spacer", "offset": 16, "stock": 11, "type": "double", "size": "font.ItemEffect.EffectText"},
    {"widget": "Pad", "offset": 28, "stock": 0.1, "value": -2.5}],
    "bytes": [{"widget": "Slot", "offset": 40, "stock": 0, "value": 1}]}
sizes = cc.load_sizes(Path(tempfile.gettempdir()) / "mr-layout-expect-none.ini") | {"magicRowHeight": 40.3, "magicIconSize": 28}
emit("bpf.uexp", bytes(uexp).hex())
emit("bpf.sizes", json.dumps(sizes))
bp_cases = [("ok", row_ok),
            ("stock", dict(row_ok, floats=[dict(row_ok["floats"][0], stock=37)])),
            ("stockf", dict(row_ok, floats=[dict(row_ok["floats"][3], stock=0.25)])),
            ("short", dict(row_ok, floats=[dict(row_ok["floats"][2], offset=60)])),
            ("byte", dict(row_ok, bytes=[dict(row_ok["bytes"][0], stock=2)])),
            ("byteshort", dict(row_ok, bytes=[dict(row_ok["bytes"][0], offset=64)]))]
for i, (label, row) in enumerate(bp_cases):
    p.LAYOUT["WBP_Test"] = row
    emit(f"bpf{i}.json", json.dumps(row))
    r, err = attempt(lambda: p.bp_float(bytes(uexp), "WBP_Test", sizes))
    emit(f"bpf{i}.out", err or r.hex())
emit("bpf.count", str(len(bp_cases)))
# layout_package: the floats and the rewrites together (a failing rewrite refuses the whole package)
e2_at = len(e0) + len(e1) + 4
row_lp = {"asset": "UI/X/WBP_Test", "floats": [{"widget": "Pad", "offset": e2_at, "stock": struct.unpack_from("<f", ux, e2_at)[0],
                                                "size": "magicRowHeight"}], "rewrites": rewrites[:2]}
row_lpfail = dict(row_lp, floats=[{"widget": "Pad", "offset": 3 + 4, "stock": 10.0, "size": "magicRowHeight"}])
for key, row in (("lp", row_lp), ("lpfail", row_lpfail)):
    p.LAYOUT["WBP_Test"] = row
    emit(f"{key}.json", json.dumps(row))
    r, err = attempt(lambda: p.layout_package(ua, ux, "WBP_Test", sizes))
    emit(f"{key}.out", err or (r[0].hex() + "|" + r[1].hex()))

# ---- [Sizes]: load_sizes and the manifest's "sizes" ----
ini = Path(tempfile.gettempdir()) / "mr-layout-expect.ini"
ini.write_text("[Colours]\nmenuTitle = 1, 2, 3\n[Sizes]\nmagicRowHeight = 40.5\ntextBodyScale = 150\nfont.ItemEffect.EffectText = 19.25 ; c\n"
               "skillIconSize = 400\n", encoding="utf-8")
emit("sizes.ini", ini.read_text(encoding="utf-8"))
emit("sizes.used", json.dumps(p.used_sizes(cc.load_sizes(ini)), indent=2))
emit("sizes.default", json.dumps(p.used_sizes(cc.load_sizes(Path(tempfile.gettempdir()) / "mr-layout-expect-none.ini")), indent=2))
ini.unlink()

# ---- write_manifest: the sizes and the header keys (header_layout, header_overflow) ----
ASSET_C, ASSET_M, ASSET_F = "UI/C/WBP_ModernMenu_ContainerInventory_MainContent", "UI/M/WBP_ModernMenu_MagicMenu", \
    "UI/F/WBP_OriginalMenu_Inventory_FilterScrollbox"
p.LAYOUT.clear()
p.LAYOUT.update({"WBP_ModernMenu_ContainerInventory_MainContent": {"asset": ASSET_C}, "WBP_ModernMenu_MagicMenu": {"asset": ASSET_M},
                 "WBP_OriginalMenu_Inventory_FilterScrollbox": {"asset": ASSET_F}})
entry = {"recipe": "layout", "stock_uexp_sha256": "a" * 64, "baked_uexp_sha256": "b" * 64}
default_sizes = p.used_sizes(cc.load_sizes(Path(tempfile.gettempdir()) / "mr-layout-expect-none.ini"))
bodies = {
    "layout": {"sizes": default_sizes, "textures": {ASSET_C: entry, ASSET_M: entry, ASSET_F: entry}, "skipped": []},
    "colour": {"style": {"menuTitle": [1.0, 2.0, 3.0]}, "textures": {ASSET_C: entry, ASSET_M: entry, ASSET_F: entry},
               "skipped": [{"asset": ASSET_F, "recipe": "layout", "reason": "r"}], "sizes": default_sizes},
    "nosizes": {"style": {"menuTitle": [1.0, 2.0, 3.0]}, "textures": {ASSET_C: entry, ASSET_M: entry}, "skipped": []},
    "noheader": {"sizes": default_sizes, "textures": {ASSET_C: entry}, "skipped": []},
}
with tempfile.TemporaryDirectory() as temp:
    folder = Path(temp)
    for ext in ("pak", "utoc", "ucas"):
        (folder / f"MRTest_P.{ext}").write_bytes(ext.encode() * 3)
    for key, body in bodies.items():
        p.write_manifest(folder, "MRTest_P", body)
        emit(f"man.{key}", (folder / "MRTest_P.json").read_text(encoding="utf-8"))


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


print("// Written by tests/ingame_pack_layout_expect.py from tools/pack_builder/pipeline.py: do not edit.")
print("static const std::pair<const char*, const char*> kExpect[] = {")
for k, v in out:
    # long hex strings split into chunks (MSVC: a string literal is at most 16380 bytes)
    chunks = [v[i:i + 8000] for i in range(0, len(v), 8000)] or [""]
    print(f"\t{{ {cstr(k)}, " + " ".join(cstr(c) for c in chunks) + " },")
print("};")
