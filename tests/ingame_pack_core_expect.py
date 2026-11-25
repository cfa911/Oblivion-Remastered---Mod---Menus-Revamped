"""Writes tests/ingame_pack_core_expect.inc: the Python recipes' results on small synthetic texture blocks (every branch: BC3,
BC1 four-colour/transparent/equal endpoints, B8G8R8A8), the oracle for tests/ingame_pack_core_tests.cpp.
  python tests/ingame_pack_core_expect.py > tests/ingame_pack_core_expect.inc
"""
import random, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from pack_builder import pipeline as p

def hdr(kind):
    h = bytearray(128); h[:4] = b"DDS "
    if kind == "DXT5": h[84:88] = b"DXT5"; return bytes(h)
    if kind == "DXT1": h[84:88] = b"DXT1"; return bytes(h)
    h[84:88] = b"DX10"; return bytes(h) + (87).to_bytes(4, "little") + bytes(16)

rnd = random.Random(7)
def blob(n): return bytes(rnd.randrange(256) for _ in range(n))
bc3 = blob(16 * 6)
bc1 = bytearray(blob(8 * 8))
# force both BC1 modes
struct.pack_into("<HH", bc1, 0, 0x8000, 0x1000)   # c0 > c1: four colours
struct.pack_into("<HH", bc1, 8, 0x1000, 0x8000)   # c0 <= c1: transparent mode
struct.pack_into("<HH", bc1, 16, 0x4208, 0x4208)  # equal
bc1 = bytes(bc1)
bgra = blob(4 * 6)
out = []
def emit(name, data, kind):
    body = data[len(hdr(kind)):]
    out.append(f'\t\t{{ "{name}", "{body.hex()}" }},')
H5, H1, HB = hdr("DXT5"), hdr("DXT1"), hdr("BGRA")
emit("flat_bc3", p.flat_blocks(H5 + bc3, (66.0, 99.0, 153.0)), "DXT5")
emit("flat_half", p.flat_blocks(H5 + bc3, (2.5, 127.5, 0.5)), "DXT5")
emit("flat_bgra", p.flat_blocks(HB + bgra, (66.0, 99.0, 153.0)), "BGRA")
emit("clear", p.clear_alpha(H5 + bc3), "DXT5")
emit("laurels_alpha", p.laurels(H5 + bc3, (66.0, 99.0, 153.0), (1.0, 1.0, 1.0, 0.6)), "DXT5")
emit("laurels_tint", p.laurels(H5 + bc3, (66.0, 99.0, 153.0), (0.5, 0.7, 1.0, 1.0)), "DXT5")
emit("panel", p.endpoint_curve(H5 + bc3, lambda b: p.panel_gray(b, 16.0, 41.0)), "DXT5")
emit("altar_bc3", p.map_colours(H5 + bc3, lambda c: p.altar_metal(c, (67.0, 75.0, 91.0))), "DXT5")
emit("altar_bc1", p.map_colours(H1 + bc1, lambda c: p.altar_metal(c, (67.0, 75.0, 91.0))), "DXT1")
emit("invert_bc1", p.map_colours(H1 + bc1, lambda c: tuple(255 - x for x in c)), "DXT1")
emit("altar_bgra", p.map_colours(HB + bgra, lambda c: p.altar_metal(c, (67.0, 75.0, 91.0))), "BGRA")
emit("tone", p.tone(H5 + bc3, (92.0, 140.0, 214.0)), "DXT5")
emit("neutral", p.neutral(H5 + bc3, (91.0, 91.0, 91.0), 59.75), "DXT5")
emit("grey_bc3", p.grey_scale(H5 + bc3, 1.2), "DXT5")
emit("grey_bc1", p.grey_scale(H1 + bc1, 0.45), "DXT1")
emit("grey_bgra", p.grey_scale(HB + bgra, 2.2), "BGRA")
emit("skin_flat", p.skin_flat(H5 + bc3, 235.0, (0.45, 0.45, 0.45, 1.0)), "DXT5")
print("\tconst std::string bc3 = \"" + bc3.hex() + "\", bc1 = \"" + bc1.hex() + "\", bgra = \"" + bgra.hex() + "\";")
print("\tstatic const Expect table[] = {")
print("\n".join(out))
print("\t};")
