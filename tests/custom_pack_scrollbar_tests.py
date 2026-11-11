"""Custom pack builder Phase 7 (owner 2026-11-11, decision 18): the scrollbar is baked, the run-time painter is gone.
- the Dark scrollbar instances hold no colours: the three [Skin] scrollbar values are baked into the stock parent materials
  M_UI_Scrollbar_Vert_Reworked (track) and M_UI_Scrollbar_Handle (thumb) by the material_colour recipe (KB
  docs/reverse-engineering/dark-mode-ui.md "Scrollbar materials": offsets and stock values, game 1.512.105.0);
- the recipe checks every stock value and skips the asset (listed) when one is not where expected;
- the values come from CustomColours.ini [Skin]; the manifest records Skin.scrollTrack, Skin.scrollBorder, Skin.scrollInside,
  so a changed key makes the pack stale and the builder marks it "Build pack";
- PaintScrollbarStyle and its callers are removed from the plugin.
retoc and the DDS tools are fakes here; `make_dark_pack.py --check` proves the real build."""
import contextlib
import hashlib
import io
import json
import struct
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace

import source_paths

fails = []
count = 0


def check(ok, why):
    global count
    count += 1
    if not ok:
        fails.append(why)


root = source_paths.ROOT
sys.path.insert(0, str(root / "tools"))
import custom_colours  # noqa: E402
import pack_builder.pipeline as pipe  # noqa: E402
from pack_builder import model  # noqa: E402

KEYS = ("scrollTrack", "scrollBorder", "scrollInside")
LIGHT_BROWN, DARK_BROWN = (0.42327, 0.30499, 0.19462, 1.0), (0.09990, 0.05613, 0.03955, 1.0)
INSIDE_DARK, INSIDE_LIGHT = (0.03560, 0.02519, 0.01850, 1.0), (0.62396, 0.58408, 0.51492, 1.0)
TRACK, THUMB = "M_UI_Scrollbar_Vert_Reworked", "M_UI_Scrollbar_Handle"
EXPECTED = {   # package: [(offset, stock, colour key)] (CachedExpressionData, then the shader map's uniform defaults)
    TRACK: [(351, LIGHT_BROWN, "Skin.scrollTrack"), (367, DARK_BROWN, "Skin.scrollTrack"), (8556, DARK_BROWN, "Skin.scrollTrack")],
    THUMB: [(329, LIGHT_BROWN, "Skin.scrollBorder"), (345, DARK_BROWN, "Skin.scrollBorder"), (361, INSIDE_DARK, "Skin.scrollInside"),
            (377, INSIDE_LIGHT, "Skin.scrollInside"), (8405, DARK_BROWN, "Skin.scrollBorder"), (8421, INSIDE_LIGHT, "Skin.scrollInside")],
}
SIZES = {TRACK: 9838, THUMB: 9675}   # stock .uexp sizes

# ---- 1. the three keys are baked Skin values ----
for key in KEYS:
    check(custom_colours.BAKED_SKIN.get(key) == f"Skin.{key}", f"{key} is a baked [Skin] value named Skin.{key}")
    check(custom_colours.pack_values().get(f"Skin.{key}") == custom_colours.SKIN[key], f"pack_values carries Skin.{key} (default)")
    help_text = custom_colours.SKIN_HELP.get(key, "")
    check("scrollbar" in help_text.lower() and "Settings" in help_text, f"{key} label says which scrollbars (all but Settings): {help_text!r}")

# ---- 2. the data rows (tools/menus/common.json) ----
rows = {}
for path in sorted((root / "tools/menus").glob("*.json")):
    data = json.loads(path.read_text(encoding="utf-8"))
    for row in data.get("assets", []):
        rows[row["asset"]] = (path.stem, row)
common = json.loads((root / "tools/menus/common.json").read_text(encoding="utf-8")) if (root / "tools/menus/common.json").is_file() else {}
check(bool(common), "tools/menus/common.json holds the shared art")
for package, colours in ((TRACK, ["Skin.scrollTrack"]), (THUMB, ["Skin.scrollBorder", "Skin.scrollInside"])):
    asset = f"UI/Materials/Common/{package}"
    menu, row = rows.get(asset, (None, {}))
    check(menu == "common" and row.get("recipe") == f"material_colour:{package}", f"{package}: common.json, recipe material_colour ({menu}, {row.get('recipe')})")
    check(row.get("colours") == colours, f"{package}: reads exactly {colours} ({row.get('colours')})")
    entries = [(e.get("offset"), tuple(e.get("stock", ())), e.get("colour")) for e in common.get("material_colours", {}).get(package, [])]
    check(len(entries) == len(EXPECTED[package]) and all(
        o == eo and c == ec and all(abs(a - b) < 5e-5 for a, b in zip(s, es))
        for (o, s, c), (eo, es, ec) in zip(entries, EXPECTED[package])), f"{package}: the probed offsets and stock values ({entries})")
    check(pipe.BLUEPRINT_COLOURS.get(package) is not None, f"{package}: the pipeline loads its literals")
material_rows = [a for a, (_, r) in rows.items() if r["recipe"].startswith("material_colour")]
check(sorted(material_rows) == sorted(f"UI/Materials/Common/{p}" for p in EXPECTED), f"only the two scrollbar materials use material_colour ({material_rows})")
for name in ("MIC_UI_Scrollbar_Vert_Dark", "MIC_UI_ScrollbarVert_Handle_Dark"):
    check(not any(Path(a).name == name for a in rows), f"{name} is not shipped (it holds no colour)")

# ---- 3. the recipe on a synthetic .uexp ----
try:
    floats, rgbs, make = pipe.recipe(f"material_colour:{THUMB}")
except Exception as error:   # noqa: BLE001
    floats, rgbs, make = (), (), None
    check(False, f"pipeline.recipe('material_colour:...') exists ({type(error).__name__}: {error})")
check(sorted((*floats, *rgbs)) == ["Skin.scrollBorder", "Skin.scrollInside"], f"the thumb recipe reads exactly its two keys ({(*floats, *rgbs)})")
try:
    check(sorted(pipe.recipe_values(f"material_colour:{TRACK}", custom_colours.pack_values())) == ["Skin.scrollTrack"], "the track recipe reads Skin.scrollTrack")
except Exception as error:   # noqa: BLE001
    check(False, f"the track recipe exists ({type(error).__name__}: {error})")


def stock_uexp(package):
    data = bytearray(hashlib.sha256(package.encode()).digest() * (SIZES[package] // 32 + 1))[:SIZES[package]]
    for offset, stock, _ in EXPECTED[package]:
        struct.pack_into("<4f", data, offset, *stock)
    return bytes(data)


if make:
    stock = stock_uexp(THUMB)
    values = {"Skin.scrollBorder": 0.0052, "Skin.scrollInside": 0.0222}
    baked = make(stock, values, None, None)
    check(len(baked) == len(stock), "the patched .uexp keeps its size")
    patched = {o for o, _, _ in EXPECTED[THUMB]}
    check(all(baked[i] == stock[i] for i in range(len(stock)) if not any(o <= i < o + 16 for o in patched)), "only the six literals change")
    for offset, _, key in EXPECTED[THUMB]:
        got = struct.unpack_from("<4f", baked, offset)
        want = (values[key],) * 3 + (1.0,)
        check(all(abs(a - b) < 1e-6 for a, b in zip(got, want)), f"@{offset}: {key} as a linear grey with alpha 1 ({got})")
    moved = bytearray(stock)
    struct.pack_into("<4f", moved, 8421, 0.5, 0.5, 0.5, 1.0)
    try:
        make(bytes(moved), values, None, None)
        check(False, "a stock value not where expected is refused")
    except pipe.SkipAsset as skip:
        check("8421" in str(skip) and "stock" in str(skip), f"the refusal names the offset and the stock ({skip})")
    except Exception as error:   # noqa: BLE001
        check(False, f"the refusal is a SkipAsset ({type(error).__name__}: {error})")

# ---- 4. values from the colour file; a changed key makes the pack stale and needs a Build ----
with tempfile.TemporaryDirectory() as temp:
    ini_path = Path(temp) / "CustomColours.ini"
    ini_path.write_text(custom_colours.render(custom_colours.DEFAULTS, custom_colours.SKIN | {"scrollTrack": 0.3}), encoding="utf-8")
    check(custom_colours.pack_values(ini_path).get("Skin.scrollTrack") == 0.3, "the build reads [Skin] scrollTrack from the colour file")
    style = dict(custom_colours.pack_values())
    m = model.ColourModel(ini_path)
    check(m.needs_build(style) == ["Skin.scrollTrack"], f"a changed scrollTrack needs a Build ({m.needs_build(style)})")
    for key in KEYS:
        check(m.entry("Skin", key).applies == "Build pack", f"{key} applies with Build pack")
    m.reset_to_dark()
    check(m.needs_build(style) == [], "Reset to Dark restores the scrollbar values")

# ---- 5. a faked build bakes both materials and records the three keys ----
BC3_DDS = b"DDS " + bytes(8) + (4).to_bytes(4, "little") * 2 + bytes(64) + b"DXT5" + bytes(40) + bytes(16)


def write_asset(folder, asset):
    path = Path(folder) / "OblivionRemastered/Content" / asset
    path.parent.mkdir(parents=True, exist_ok=True)
    path.with_suffix(".uasset").write_bytes(b"uasset " + asset.encode())
    name = Path(asset).name
    path.with_suffix(".uexp").write_bytes(stock_uexp(name) if name in EXPECTED else b"uexp " + asset.encode() + bytes(4096))


class Fake:
    def run(self, cmd, *rest, **kw):
        cmd = [str(c) for c in cmd]
        if cmd[1:2] == ["to-legacy"]:
            names = {cmd[i + 1] for i, c in enumerate(cmd) if c == "-f"}
            for asset in pipe.TEXTURES:
                if Path(asset).name in names:
                    write_asset(cmd[-1], asset)
            for masks in pipe.GLYPH_MASKS.values():
                for name in masks:
                    write_asset(cmd[-1], f"Art/UI/FakeMasks/{name}")
            return ""
        if cmd[1:2] == ["to-zen"]:
            utoc = Path(cmd[-1])
            utoc.parent.mkdir(parents=True, exist_ok=True)
            for ext in ("pak", "utoc", "ucas"):
                utoc.with_suffix("." + ext).write_bytes(b"pack")
            return ""
        raise AssertionError(cmd)

    def export(self, uasset, dds):
        Path(dds).parent.mkdir(parents=True, exist_ok=True)
        Path(dds).write_bytes(BC3_DDS)

    def inject(self, uasset, dds, out):
        Path(out).parent.mkdir(parents=True, exist_ok=True)
        Path(out).write_bytes(b"injected")
        Path(out).with_suffix(".uexp").write_bytes(b"injected")


REAL = pipe.recipe


def fake_recipe(name):
    floats, rgbs, real = REAL(name)
    if name.startswith("material_colour"):
        return floats, rgbs, real   # the real patch on the synthetic stock
    return floats, rgbs, lambda data, values, work, stock: data.read_bytes() if hasattr(data, "read_bytes") else data


fake = Fake()
saved = {k: getattr(pipe, k, None) for k in ("run", "recipe", "dds_tools", "LAUREL_STOCK_SHA256", "ALCHEMY_STOCK_SHA256")}
pipe.run, pipe.recipe, pipe.dds_tools = fake.run, fake_recipe, (lambda path=None: (fake.export, fake.inject))
synthetic = {a: hashlib.sha256(b"uexp " + a.encode() + bytes(4096)).hexdigest() for a in pipe.TEXTURES}
pipe.LAUREL_STOCK_SHA256 = next(h for a, h in synthetic.items() if pipe.TEXTURES[a] == "laurels")
pipe.ALCHEMY_STOCK_SHA256 = {Path(a).name: h for a, h in synthetic.items() if pipe.TEXTURES[a] == "alchemy"}
try:
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        retoc = temp / "retoc.exe"
        retoc.write_bytes(b"")
        (temp / "no-art").mkdir()
        with contextlib.redirect_stdout(io.StringIO()):
            manifest = pipe.build(SimpleNamespace(retoc=retoc, paks=temp / "Paks", ddstools=None), temp / "out", art=temp / "no-art")
        check(manifest["skipped"] == [], f"nothing skipped ({manifest['skipped']})")
        for package in EXPECTED:
            entry = manifest["textures"].get(f"UI/Materials/Common/{package}", {})
            check(entry.get("recipe") == f"material_colour:{package}", f"{package} is baked into the pack")
        for key in KEYS:
            check(manifest["style"].get(f"Skin.{key}") == custom_colours.SKIN[key], f"the manifest style records Skin.{key}")
except Exception as error:   # noqa: BLE001
    check(False, f"the faked build finishes ({type(error).__name__}: {error})")
finally:
    for k, v in saved.items():
        if v is None:
            if hasattr(pipe, k):
                delattr(pipe, k)
        else:
            setattr(pipe, k, v)

# ---- 6. the run-time painter is gone ----
src = "\n".join(p.read_text(encoding="utf-8", errors="replace") for p in sorted((root / "src").rglob("*.cpp")) + sorted((root / "src").rglob("*.h")))
check("PaintScrollbarStyle" not in src, "PaintScrollbarStyle is removed")
check("StyleListScrollbar" not in src, "StyleListScrollbar and its callers are removed")
check('L"DarkColor"' not in src and 'L"InsideColor - Dark"' not in src, "no scrollbar material parameter is set at run time")
check("WidgetBarStyle" not in src, "the details perk scrollbox is no longer painted")

print(f"{count} checks, {len(fails)} failures")
for why in fails:
    print("FAIL:", why)
sys.exit(1 if fails else 0)
