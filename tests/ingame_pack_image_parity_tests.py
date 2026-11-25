"""In-game Pack Builder Phase 3b: the Pillow port (src/builder/PackImage.h, PackImageCodec.h) gives Pillow 13.0.0's bytes for
every operation the Python Pack Builder uses, on seeded random images of the recipes' sizes and modes and on the real stock
textures of the installed game.

The C++ driver (tests/ingame_pack_image_parity_driver.cpp) runs each job; Pillow runs the same operation here; every output
(image mode, size and pixels, BC blocks, query results) must be identical.

Stock textures (one of):
  MR_IMAGE_STOCK   a folder of stock .dds exports (for example the work/dds folder of tests/pack_pipeline_oracle.py)
  MR_PAKS, MR_RETOC, MR_DDSTOOLS   extract and export them like tests/ingame_pack_parity_tests.py
Without either, only the random part runs and the suite fails (no stock coverage). cl on PATH (run_suites.py --msvc) or
MR_MSVC_SETUP. Optional MR_PARITY_WORK: a work folder to keep. Run without -I (Pillow is in the user site-packages).
"""
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace

import PIL
from PIL import Image, ImageChops, ImageDraw, ImageFilter

import source_paths

root = source_paths.ROOT
sys.path.insert(0, str(root / "tools"))
import dds_image  # noqa: E402

MODES = ("L", "LA", "RGB", "RGBA")
FILTERS = {"NEAREST": Image.NEAREST, "BOX": Image.Resampling.BOX, "BILINEAR": Image.BILINEAR, "LANCZOS": Image.LANCZOS}
fails, count, per_op = [], 0, {}


def check(ok, why, op="setup"):
    global count
    count += 1
    total, bad = per_op.get(op, (0, 0))
    per_op[op] = (total + 1, bad + (0 if ok else 1))
    if not ok:
        fails.append(why)


def finish():
    for op in sorted(per_op):
        print(f"  {op:10} {per_op[op][0]:6} checks {per_op[op][1]:5} failures")
    for f in fails[:40]:
        print("FAIL", f)
    print(f"{count} checks, {len(fails)} failures")
    sys.exit(1 if fails else 0)


def compile_driver(work):
    exe = work / "image_driver.exe"
    src = root / "tests/ingame_pack_image_parity_driver.cpp"
    cmd = f'cl /nologo /std:c++latest /O2 /EHsc /utf-8 /I"{root / "src"}" "{src}" /Fe"{exe}" /Fo"{work}\\\\"'
    if not shutil.which("cl"):
        setup = os.environ.get("MR_MSVC_SETUP")
        if not setup:
            check(False, "no cl on PATH and no MR_MSVC_SETUP")
            finish()
        cmd = f'call "{setup}" >nul 2>nul && {cmd}'
    r = subprocess.run(f'cmd /d /s /c "{cmd}"', capture_output=True, text=True, errors="replace")
    if r.returncode or not exe.is_file():
        check(False, "driver compile failed: " + (r.stdout + r.stderr)[-800:])
        finish()
    return exe


def save_pki(path, im):
    path.write_bytes(b"PKI1" + bytes((MODES.index(im.mode), 0, 0, 0)) + struct.pack("<II", *im.size) + im.tobytes())


def load_pki(path):
    d = path.read_bytes()
    if len(d) < 16 or d[:4] != b"PKI1":
        return None
    return MODES[d[4]], struct.unpack_from("<II", d, 8), d[16:]


class Batch:
    """Jobs for one driver run: the driver line, its output file and the Pillow result to compare with."""

    def __init__(self, work, exe):
        self.work, self.exe, self.jobs, self.n = work, exe, [], 0

    def name(self, ext="pki"):
        self.n += 1
        return f"f{self.n}.{ext}"

    def image(self, im):
        n = self.name()
        save_pki(self.work / n, im)
        return n

    def add(self, op, line, expected, kind="image", what=""):
        """expected: a function returning the Pillow result (an Image, bytes or text) or None for 'refused'."""
        out = self.name("out")
        self.jobs.append((op, f"{line} {out}" if "{out}" not in line else line.replace("{out}", out), out, expected, kind, what))

    def run(self):
        if not self.jobs:
            return
        (self.work / "jobs.txt").write_text("\n".join(j[1] for j in self.jobs) + "\n", encoding="utf-8")
        r = subprocess.run([str(self.exe), "jobs.txt"], cwd=self.work, capture_output=True, text=True, errors="replace")
        if r.returncode:
            check(False, f"driver failed ({r.returncode}): {(r.stdout + r.stderr)[-400:]}", "driver")
        for op, line, out, expected, kind, what in self.jobs:
            path = self.work / out
            got = path.read_bytes() if path.is_file() else None
            exp = expected()
            label = f"{op} {what or line[:90]}"
            if kind == "image":
                if exp is None:
                    check(got == b"", f"{label}: Pillow refuses, C++ gave an image", op)
                    continue
                mine = load_pki(path) if got else None
                ok = mine is not None and mine == (exp.mode, exp.size, exp.tobytes())
                if not ok and mine is not None and mine[:2] == (exp.mode, exp.size):
                    a, b = mine[2], exp.tobytes()
                    diff = sum(1 for x, y in zip(a, b) if x != y)
                    first = next(i for i in range(len(b)) if a[i] != b[i])
                    label += f" ({diff} bytes differ, first at {first}: {a[first]} vs Pillow {b[first]})"
                elif not ok:
                    label += f" (C++ {mine[:2] if mine else 'none'}, Pillow {(exp.mode, exp.size)})"
                check(ok, label, op)
            else:
                exp = exp if isinstance(exp, bytes) else str(exp).encode()
                check(got == exp, f"{label}: {got[:40] if got else got} vs Pillow {exp[:40]}", op)
        for f in self.work.iterdir():
            if f.suffix in (".pki", ".out", ".png", ".dds", ".bin"):
                f.unlink()
        self.jobs, self.n = [], 0


def colors_bytes(im, maxcolors):
    c = im.getcolors(maxcolors)
    if c is None:
        return b"none"
    out = b""
    for n, p in c:
        pixel = p if isinstance(p, int) else int.from_bytes(bytes(p) + bytes(4 - len(p)), "little")
        out += struct.pack("<II", n, pixel)
    return out


def bc_bytes(im, fmt):
    import io
    buf = io.BytesIO()
    im.save(buf, "DDS", pixel_format=fmt)
    return buf.getvalue()[128:]


# ---------------------------------------------------------------- random inputs

def noise(rnd, mode, w, h):
    return Image.frombytes(mode, (w, h), rnd.randbytes(w * h * len(mode)))


def structured(rnd, mode, w, h):
    """Flat rectangles, a gradient and noise; alpha often exactly 0 or 255 (the textures' look, and the premultiply edges)."""
    def colour(pick):
        c = tuple(pick() for _ in mode)
        return c[0] if len(c) == 1 else c

    im = Image.new(mode, (w, h), colour(lambda: rnd.randrange(256)))
    draw = ImageDraw.Draw(im)
    for _ in range(rnd.randrange(1, 6)):
        x0, y0 = rnd.randrange(w), rnd.randrange(h)
        draw.rectangle((x0, y0, rnd.randrange(x0, w), rnd.randrange(y0, h)), fill=colour(lambda: rnd.choice((0, 255, rnd.randrange(256)))))
    if rnd.random() < 0.5:
        bands = list(im.split())
        k = rnd.randrange(len(bands))
        bands[k] = Image.linear_gradient("L").resize((w, h)) if rnd.random() < 0.5 else noise(rnd, "L", w, h)
        im = Image.merge(mode, bands)
    return im


def any_image(rnd, mode, w, h):
    return noise(rnd, mode, w, h) if rnd.random() < 0.5 else structured(rnd, mode, w, h)


SIZES = [(1, 1), (2, 2), (1, 7), (7, 1), (3, 5), (4, 4), (5, 3), (16, 16), (41, 1), (42, 2), (2, 128), (37, 29), (64, 64),
         (127, 33), (128, 128), (256, 64), (257, 257), (1024, 128)]


def random_jobs(b, rnd):
    def size():
        return rnd.choice(SIZES)

    # convert, every pair of modes
    for src in MODES:
        for dst in MODES:
            if src != dst:
                for _ in range(3):
                    im = any_image(rnd, src, *size())
                    b.add("convert", f"conv {b.image(im)} {dst}", lambda im=im, dst=dst: im.convert(dst), what=f"{src}->{dst} {im.size}")
    # raw byte orders (frombuffer BGRA, tobytes BGRA)
    for _ in range(4):
        w, h = size()
        raw = rnd.randbytes(w * h * 4)
        n = b.name("bin")
        (b.work / n).write_bytes(raw)
        b.add("raw", f"raw {n} RGBA {w} {h} BGRA", lambda raw=raw, w=w, h=h: Image.frombuffer("RGBA", (w, h), raw, "raw", "BGRA", 0, 1).copy())
        im = noise(rnd, "RGBA", w, h)
        b.add("raw", f"tobytes {b.image(im)} BGRA", lambda im=im: im.tobytes("raw", "BGRA"), kind="bytes")
    # resize: the recipes' filters on their modes, down, up, mip chains, odd sizes
    for name, modes in (("LANCZOS", ("RGBA", "RGB", "L", "LA")), ("BILINEAR", ("L", "RGBA")), ("BOX", ("L",)), ("NEAREST", ("L", "RGBA"))):
        for _ in range(60):
            mode = rnd.choice(modes)
            w, h = size()
            im = any_image(rnd, mode, w, h)
            tw, th = rnd.choice(((max(1, w // 2), max(1, h // 2)), (rnd.randrange(1, 300), rnd.randrange(1, 140)), (w, 1), (1, h),
                                 (w * 3, h), (max(1, w // 4), max(1, h // 4)), (rnd.randrange(1, 9), rnd.randrange(1, 9))))
            b.add("resize", f"resize {b.image(im)} {tw} {th} {name}", lambda im=im, t=(tw, th), f=FILTERS[name]: im.resize(t, f),
                  what=f"{name} {mode} {im.size}->{(tw, th)}")
    im = structured(rnd, "RGBA", 256, 128)
    n = b.image(im)
    w, h = im.size
    while True:   # one full mip chain, like colour_keep_alpha
        b.add("resize", f"resize {n} {w} {h} LANCZOS", lambda im=im, t=(w, h): im.resize(t, Image.LANCZOS), what=f"mip {w}x{h}")
        if (w, h) == (1, 1):
            break
        w, h = max(1, w // 2), max(1, h // 2)
    # blurs: GaussianBlur radii from the colour files (any float), BoxBlur radii of the recipes and fractions
    for _ in range(60):
        im = any_image(rnd, "L", *size())
        r = rnd.choice((0.6, 1.0, 1.2, 1.5, 2.0, 3.0, round(rnd.uniform(0.05, 14), rnd.choice((1, 2, 3))), rnd.uniform(0.3, 6)))
        b.add("gauss", f"gauss {b.image(im)} {r!r}", lambda im=im, r=r: im.filter(ImageFilter.GaussianBlur(r)), what=f"r={r} {im.size}")
    for _ in range(25):
        im = any_image(rnd, "L", *size())
        r = rnd.choice((1, 2, 4, 8, 0.5, 2.5, round(rnd.uniform(0.1, 20), 2)))
        b.add("boxblur", f"boxblur {b.image(im)} {r!r}", lambda im=im, r=r: im.filter(ImageFilter.BoxBlur(r)), what=f"r={r} {im.size}")
    for mode in ("RGB", "RGBA"):
        im = any_image(rnd, mode, 37, 29)
        b.add("gauss", f"gauss {b.image(im)} 2.2", lambda im=im: im.filter(ImageFilter.GaussianBlur(2.2)), what=f"{mode}")
        b.add("boxblur", f"boxblur {b.image(im)} 3", lambda im=im: im.filter(ImageFilter.BoxBlur(3)), what=f"{mode}")
    # rank filters: the recipes' sizes
    for filt, sizes in (("median", (3, 9, 15, 41)), ("max", (5, 21, 2 * 6 + 1)), ("min", (3, 9)), ("rank", (3, 5, 7))):
        for s in sizes:
            for _ in range(3):
                im = any_image(rnd, "L", *rnd.choice([z for z in SIZES if z[0] * z[1] <= 40000]))
                rank = {"median": s * s // 2, "max": s * s - 1, "min": 0, "rank": rnd.randrange(s * s)}[filt]
                pil = {"median": lambda: ImageFilter.MedianFilter(s), "max": lambda: ImageFilter.MaxFilter(s),
                       "min": lambda: ImageFilter.MinFilter(s), "rank": lambda: ImageFilter.RankFilter(s, rank)}[filt]()
                b.add("rank", f"rank {b.image(im)} {s} {rank}", lambda im=im, pil=pil: im.filter(pil), what=f"{filt} {s} {im.size}")
    # chops and composites
    for _ in range(8):
        w, h = size()
        x, y = any_image(rnd, "L", w, h), any_image(rnd, "L", w, h)
        b.add("chops", f"mul {b.image(x)} {b.image(y)}", lambda x=x, y=y: ImageChops.multiply(x, y))
        b.add("chops", f"dark {b.image(x)} {b.image(y)}", lambda x=x, y=y: ImageChops.darker(x, y))
    for _ in range(12):
        mode = rnd.choice(("RGB", "RGBA", "L"))
        w, h = size()
        x, y, m = any_image(rnd, mode, w, h), any_image(rnd, mode, w, h), any_image(rnd, "L", w, h)
        b.add("composite", f"comp {b.image(x)} {b.image(y)} {b.image(m)}", lambda x=x, y=y, m=m: Image.composite(x, y, m), what=mode)
    for _ in range(12):
        w, h = size()
        x, y = any_image(rnd, "RGBA", w, h), any_image(rnd, "RGBA", w, h)
        b.add("alphacomp", f"acomp {b.image(x)} {b.image(y)}", lambda x=x, y=y: Image.alpha_composite(x, y))
        dst = any_image(rnd, "RGBA", *size())
        src = any_image(rnd, "RGBA", *size())
        dx, dy = rnd.randrange(dst.width + 2), rnd.randrange(dst.height + 2)

        def at(dst=dst, src=src, d=(dx, dy)):
            out = dst.copy()
            out.alpha_composite(src, d)
            return out
        b.add("alphacomp", f"acompat {b.image(dst)} {b.image(src)} {dx} {dy}", at, what=f"{src.size} at {(dx, dy)} on {dst.size}")
    # point, crop, paste, fills, bands
    for _ in range(6):
        im = any_image(rnd, "L", *size())
        lut = [rnd.randrange(256) for _ in range(256)]
        b.add("point", f"point {b.image(im)} {bytes(lut).hex()}", lambda im=im, lut=lut: im.point(lut))
    for _ in range(10):
        im = any_image(rnd, rnd.choice(MODES), *size())
        box = sorted((rnd.randrange(-3, im.width + 3), rnd.randrange(-3, im.width + 3)))
        boy = sorted((rnd.randrange(-3, im.height + 3), rnd.randrange(-3, im.height + 3)))
        box = (box[0], boy[0], box[1] + 1, boy[1] + 1)
        b.add("crop", f"crop {b.image(im)} {' '.join(map(str, box))}", lambda im=im, box=box: im.crop(box))
    for _ in range(10):
        mode = rnd.choice(MODES)
        dst, src = any_image(rnd, mode, *size()), any_image(rnd, mode, *size())
        x, y = rnd.randrange(-src.width, dst.width + 1), rnd.randrange(-src.height, dst.height + 1)

        def pasted(dst=dst, src=src, p=(x, y)):
            out = dst.copy()
            out.paste(src, p)
            return out
        b.add("paste", f"paste {b.image(dst)} {b.image(src)} {x} {y}", pasted)
        box = (rnd.randrange(-2, dst.width), rnd.randrange(-2, dst.height))
        box = box + (rnd.randrange(box[0] + 1, dst.width + 3), rnd.randrange(box[1] + 1, dst.height + 3))
        colour = tuple(rnd.randrange(256) for _ in range(4))

        def filled(dst=dst, box=box, c=colour[:len(mode)]):
            out = dst.copy()
            out.paste(c if len(c) > 1 else c[0], box)
            return out

        def rect(dst=dst, box=box, c=colour[:len(mode)]):
            out = dst.copy()
            ImageDraw.Draw(out).rectangle((box[0], box[1], box[2] - 1, box[3] - 1), fill=c if len(c) > 1 else c[0])
            return out
        cs = " ".join(map(str, colour))
        b.add("paste", f"fill {b.image(dst)} {' '.join(map(str, box))} {cs}", filled, what=f"fill {mode} {box}")
        b.add("paste", f"rect {b.image(dst)} {box[0]} {box[1]} {box[2] - 1} {box[3] - 1} {cs}", rect, what=f"rect {mode} {box}")
    for _ in range(6):
        mode = rnd.choice(("RGBA", "RGB", "L", "LA"))
        im, a = any_image(rnd, mode, 9, 7), any_image(rnd, "L", 9, 7)

        def putalpha(im=im, a=a):
            out = im.copy()
            out.putalpha(a)
            return out
        b.add("bands", f"putalpha {b.image(im)} {b.image(a)}", putalpha, what=mode)
        k = rnd.randrange(len(mode))
        b.add("bands", f"chan {b.image(im)} {k}", lambda im=im, k=k: im.getchannel(k), what=f"{mode} band {k}")
        parts = im.split()
        b.add("bands", f"merge {mode} {{out}} " + " ".join(b.image(p) for p in parts), lambda im=im: Image.merge(im.mode, im.split()))
    # queries
    for _ in range(12):
        mode = rnd.choice(MODES)
        im = Image.new(mode, size(), 0)
        for _ in range(rnd.randrange(4)):
            im.putpixel((rnd.randrange(im.width), rnd.randrange(im.height)), tuple(rnd.randrange(256) for _ in mode) if len(mode) > 1 else rnd.randrange(256))
        bbox = im.getbbox()
        b.add("query", f"bbox {b.image(im)}", lambda bbox=bbox: "none" if bbox is None else " ".join(map(str, bbox)), kind="text", what=f"bbox {mode}")
        g = any_image(rnd, "L", *size())
        b.add("query", f"extrema {b.image(g)}", lambda g=g: " ".join(map(str, g.getextrema())), kind="text", what="extrema")
    for _ in range(12):
        mode = rnd.choice(("RGBA", "RGB", "L", "RGBA"))
        im = noise(rnd, mode, *size()) if rnd.random() < 0.4 else structured(rnd, mode, *size())
        mc = rnd.choice((1 << 24, 256, 5))
        b.add("colors", f"colors {b.image(im)} {mc}", lambda im=im, mc=mc: colors_bytes(im, mc), kind="bytes", what=f"{mode} {im.size} max {mc}")
    # BC encode
    for _ in range(40):
        fmt = rnd.choice(("DXT1", "DXT5"))
        mode = "RGB" if fmt == "DXT1" else rnd.choice(("RGBA", "RGBA", "RGB"))
        im = any_image(rnd, mode, *size())
        b.add("bc", f"bc {b.image(im)} {fmt}", lambda im=im, fmt=fmt: bc_bytes(im, fmt), kind="bytes", what=f"{fmt} {mode} {im.size}")
    # DDS decode of random blocks (both BC1 colour modes, both BC3 alpha modes)
    for _ in range(12):
        w, h = size()
        fourcc, dxgi = rnd.choice(((b"DXT1", 0), (b"DXT5", 0), (b"DX10", 71), (b"DX10", 77), (b"DX10", 70), (b"DX10", 76)))
        bc3 = fourcc == b"DXT5" or dxgi in (76, 77)
        head = (b"DDS " + struct.pack("<7I", 124, 0x81007, h, w, 0, 0, 0) + bytes(44) + struct.pack("<4I", 32, 4, struct.unpack("<I", fourcc)[0], 0)
                + bytes(16) + struct.pack("<5I", 0x1000, 0, 0, 0, 0) + (struct.pack("<5I", dxgi, 3, 0, 1, 0) if dxgi else b""))
        data = head + rnd.randbytes(((w + 3) // 4) * ((h + 3) // 4) * (16 if bc3 else 8))
        n = b.name("dds")
        (b.work / n).write_bytes(data)

        def dec(data=data):
            import io
            return Image.open(io.BytesIO(data)).convert("RGBA")
        b.add("dds", f"dds {n}", dec, what=f"{fourcc} {dxgi} {(w, h)}")
    # PNG written by Pillow: every mode, compression level, optimize
    for mode in MODES:
        for level in (0, 1, 6, 9):
            im = any_image(rnd, mode, *size())
            n = b.name("png")
            im.save(b.work / n, "PNG", compress_level=level, optimize=level == 9)
            b.add("png", f"png {n}", lambda p=b.work / n: Image.open(p).copy(), what=f"{mode} level {level}")


# ---------------------------------------------------------------- stock textures

def stock_folder(work):
    given = os.environ.get("MR_IMAGE_STOCK")
    if given:
        return Path(given)
    env = {k: os.environ.get(k) for k in ("MR_PAKS", "MR_RETOC", "MR_DDSTOOLS")}
    if not all(env.values()):
        return None
    from pack_builder import pipeline
    legacy, dds = work / "legacy", work / "stock-dds"
    dds.mkdir(parents=True, exist_ok=True)
    args = SimpleNamespace(paks=Path(env["MR_PAKS"]), retoc=Path(env["MR_RETOC"]), ddstools=Path(env["MR_DDSTOOLS"]))
    found = pipeline.extract_all(args, legacy, colours=True, layout=False)
    export, _ = pipeline.dds_tools(args.ddstools)
    for name in sorted({Path(a).stem.lower() for a in pipeline.TEXTURES} & set(found)):
        try:
            export(found[name][0], dds / (found[name][0].stem + ".dds"))
        except Exception as error:   # not a texture
            print("skip", name, error)
    return dds


def stock_jobs(b, path, k, rnd):
    data = path.read_bytes()
    dxgi = struct.unpack_from("<I", data, 128)[0] if data[84:88] == b"DX10" else None
    if dxgi in (87, 91):   # raw BGRA: FromRaw like dds_image.load; DecodeDds refuses it (no Pillow BC path)
        h, w = struct.unpack_from("<II", data, 12)
        n = b.name("bin")
        (b.work / n).write_bytes(data[148:148 + w * h * 4])
        b.add("stock-dec", f"raw {n} RGBA {w} {h} BGRA", lambda: dds_image.load(path), what=path.name)
        n = b.name("dds")
        (b.work / n).write_bytes(data)
        b.add("stock-dec", f"dds {n}", lambda: None, what=path.name + " refused")
        im = dds_image.load(path)
    else:
        n = b.name("dds")
        (b.work / n).write_bytes(data)
        b.add("stock-dec", f"dds {n}", lambda: Image.open(path).convert("RGBA"), what=path.name)
        im = Image.open(path).convert("RGBA")
    rgba = b.image(im)
    grey_im = im.convert("L")
    alpha_im = im.getchannel("A")
    grey, alpha = b.image(grey_im), b.image(alpha_im)
    w, h = im.size
    b.add("stock-conv", f"conv {rgba} L", lambda: im.convert("L"), what=path.name)
    b.add("stock-conv", f"conv {rgba} RGB", lambda: im.convert("RGB"), what=path.name)
    b.add("stock-query", f"colors {rgba} {1 << 24}", lambda: colors_bytes(im, 1 << 24), kind="bytes", what=path.name)
    b.add("stock-query", f"bbox {alpha}", lambda: (lambda x: "none" if x is None else " ".join(map(str, x)))(alpha_im.getbbox()), kind="text", what=path.name)
    r = (0.6, 1.0, 1.2, 2.5, 4.0, 7.3)[k % 6]
    b.add("stock-gauss", f"gauss {grey} {r!r}", lambda: grey_im.filter(ImageFilter.GaussianBlur(r)), what=f"{path.name} r={r}")
    br = (1, 4, 8, 2.5)[k % 4]
    b.add("stock-box", f"boxblur {grey} {br!r}", lambda: grey_im.filter(ImageFilter.BoxBlur(br)), what=f"{path.name} r={br}")
    if w * h <= 1 << 19:
        s = (3, 9, 15, 21)[k % 4]
        b.add("stock-rank", f"rank {grey} {s} {s * s // 2}", lambda: grey_im.filter(ImageFilter.MedianFilter(s)), what=f"{path.name} median {s}")
        b.add("stock-rank", f"rank {alpha} 5 24", lambda: alpha_im.filter(ImageFilter.MaxFilter(5)), what=f"{path.name} max 5")
    b.add("stock-resize", f"resize {grey} {max(1, w * 3 // 4)} {max(1, h * 3 // 4)} BILINEAR",
          lambda: grey_im.resize((max(1, w * 3 // 4), max(1, h * 3 // 4)), Image.BILINEAR), what=path.name)
    b.add("stock-resize", f"resize {grey} {w} 1 BOX", lambda: grey_im.resize((w, 1), Image.Resampling.BOX), what=path.name)
    b.add("stock-chops", f"mul {grey} {alpha}", lambda: ImageChops.multiply(grey_im, alpha_im), what=path.name)
    white = Image.new("RGBA", im.size, (255, 255, 255, 255))
    b.add("stock-comp", f"acomp {b.image(white)} {rgba}", lambda: Image.alpha_composite(white, im), what=path.name)
    rgb_im = im.convert("RGB")
    rgb = b.image(rgb_im)
    backing_im = Image.new("RGB", im.size, (rnd.randrange(256), rnd.randrange(256), rnd.randrange(256)))
    b.add("stock-comp", f"comp {rgb} {b.image(backing_im)} {alpha}", lambda: Image.composite(rgb_im, backing_im, alpha_im), what=path.name)
    # the mip chain the recipes write: each level resized from the top (LANCZOS), then BC-encoded by Pillow
    bc1 = dxgi in (70, 71) or data[84:88] == b"DXT1"
    src, src_im, fmt = (rgb, rgb_im, "DXT1") if bc1 else (rgba, im, "DXT5")
    mw, mh = w, h
    while True:
        level_im = src_im.resize((mw, mh), Image.LANCZOS)
        b.add("stock-resize", f"resize {src} {mw} {mh} LANCZOS", lambda l=level_im: l, what=f"{path.name} {mw}x{mh}")
        b.add("stock-bc", f"bc {b.image(level_im)} {fmt}", lambda l=level_im, f=fmt: bc_bytes(l, f), kind="bytes", what=f"{path.name} {fmt} {mw}x{mh}")
        if (mw, mh) == (1, 1):
            break
        mw, mh = max(1, mw // 2), max(1, mh // 2)


def main():
    keep = os.environ.get("MR_PARITY_WORK")
    work = Path(keep) if keep else Path(tempfile.mkdtemp(prefix="mr-image-parity-"))
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    try:
        check(PIL.__version__ == "13.0.0", f"Pillow {PIL.__version__}, the port is of 13.0.0")
        b = Batch(work, compile_driver(work))
        rnd = random.Random(3)
        for _ in range(3):
            random_jobs(b, rnd)
            b.run()
        # the PNGs the builder reads (data folder) and the owner art
        for p in sorted((root / "tools/menus").glob("*.png")) + sorted((root / "assets-src/DarkPack").glob("*.png")):
            n = b.name("png")
            shutil.copyfile(p, work / n)
            b.add("png-shipped", f"png {n}", lambda p=p: Image.open(p).copy(), what=p.name)
        b.run()
        stock = stock_folder(work)
        files = sorted(stock.rglob("*.dds")) if stock else []
        check(len(files) > 100, f"stock textures: {len(files)} found (set MR_IMAGE_STOCK or MR_PAKS/MR_RETOC/MR_DDSTOOLS)")
        for k, path in enumerate(files):
            stock_jobs(b, path, k, rnd)
            if k % 12 == 11:
                b.run()
        b.run()
        print(f"{len(files)} stock textures")
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)
    finish()


if __name__ == "__main__":
    main()
