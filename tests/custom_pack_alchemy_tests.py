"""Custom pack builder Phase 6 (owner 2026-11-11, decisions 14-15): the three Alchemy textures that used the owner's own art
(T_UI_Alchemy_Background, T_Alchemy_IngredientsSlots_BG, T_Alchemy_mortar_pestle) are rebuilt from the stock textures by the
`alchemy` recipe, so a user build needs no owner art: 69 baked, 0 skipped.
- two shared [Colours] keys: alchemyMetal (the bronze frame, slot rims and pot rim) and alchemyStone (the stone inside);
- the stock .uexp hash is checked (like the laurel recipe), a different stock asset is skipped and listed;
- the stock BC3 alpha blocks and the DDS header stay byte for byte; metal and stone follow their own colour;
- the colours come from the colour file and a changed key makes the pack stale;
- no data file row uses the png recipe any more;
- a first build with no Oodle DLL and no internet, or a read-only retoc folder, ends with a clear message and leaves the
  installed pack as it was.
retoc and the DDS tools are fakes here; `make_dark_pack.py --check` proves the real build."""
import contextlib
import hashlib
import importlib
import io
import json
import subprocess
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
from pack_builder import app, model  # noqa: E402

from PIL import Image  # noqa: E402

ALCHEMY = {
    "T_UI_Alchemy_Background": "10989a5b51e32941b2d8940adee7dc6251502535a2b5191c8486f936c38891a9",
    "T_Alchemy_IngredientsSlots_BG": "02e3d4bb82c118d5fa01cf69b4bdd1a3c876d48c02ca32b2ce46f894b069fa99",
    "T_Alchemy_mortar_pestle": "9643a8a439f01440da156b79b40ac4d77a909365dd6b11cbd55d922ffdbcb659",
}   # stock .uexp SHA-256, game 1.512.105.0 (KB docs/alchemy/internals.md)
METAL, STONE = [56.0, 83.0, 118.0], [22.0, 25.0, 31.0]   # the owner art's averages (KB plan page, Phase 6)

# ---- 1. the colour keys ----
check(custom_colours.COLOURS.get("alchemyMetal") == METAL, f"alchemyMetal default = {METAL} ({custom_colours.COLOURS.get('alchemyMetal')})")
check(custom_colours.COLOURS.get("alchemyStone") == STONE, f"alchemyStone default = {STONE} ({custom_colours.COLOURS.get('alchemyStone')})")
for key in ("alchemyMetal", "alchemyStone"):
    check(custom_colours.HELP.get(key, "").startswith("Alchemy "), f"{key} is labelled with its menu (Phase 5 style)")
dark = (root / "src/core/DarkStyle.h").read_text(encoding="utf-8")
check(custom_colours.from_darkstyle(dark).get("alchemyMetal") == METAL
      and custom_colours.from_darkstyle(dark).get("alchemyStone") == STONE, "DarkStyle.h holds both defaults")
reader = (root / "src/core/rules/CustomColours.h").read_text(encoding="utf-8")
check("MR_CC_RGB(alchemyMetal," in reader and "MR_CC_RGB(alchemyStone," in reader,
      "the plugin's colour reader knows both keys (it writes the default file and checks the pack)")
ini = (root / "config/CustomColours.ini").read_text(encoding="utf-8")
check("alchemyMetal = 56, 83, 118" in ini and "alchemyStone = 22, 25, 31" in ini, "config/CustomColours.ini lists both keys")
check(ini == custom_colours.render(custom_colours.DEFAULTS), "config/CustomColours.ini is the rendered default file")

# ---- 2. the data rows ----
rows = {}
for path in sorted((root / "tools/menus").glob("*.json")):
    for row in json.loads(path.read_text(encoding="utf-8")).get("assets", []):
        rows[row["asset"]] = (path.stem, row)
png = [a for a, (_, r) in rows.items() if r["recipe"] == "png"]
check(not png, f"no data file row uses the png recipe (owner art) ({png})")
alchemy_rows = {Path(a).name: (menu, r) for a, (menu, r) in rows.items() if Path(a).name in ALCHEMY}
check(sorted(alchemy_rows) == sorted(ALCHEMY), "the three Alchemy textures are still in the data files")
for name, (menu, row) in alchemy_rows.items():
    check(menu == "alchemy" and row["recipe"] == "alchemy", f"{name}: alchemy.json, recipe alchemy ({menu}, {row['recipe']})")
    check(row["colours"] == ["alchemyMetal", "alchemyStone"], f"{name}: reads alchemyMetal and alchemyStone ({row['colours']})")
check(len(rows) == 191, f"191 assets after 28b, the 2026-11-13 Inventory bakes, RMI-096 popups and RMI-097 tabs ({len(rows)})")

# ---- 3. the recipe on a synthetic BC3 texture: bronze top half, green-grey stone bottom half, a soft alpha ----
try:
    floats, rgbs, make = pipe.recipe("alchemy")
except Exception as error:   # noqa: BLE001
    floats, rgbs, make = (), (), None
    check(False, f"pipeline.recipe('alchemy') exists ({type(error).__name__}: {error})")
check(sorted((*floats, *rgbs)) == ["alchemyMetal", "alchemyStone"], "the alchemy recipe reads exactly the two keys")


def synthetic(folder):
    image = Image.new("RGBA", (16, 16))
    pixels = []
    for y in range(16):
        for x in range(16):
            shade = 0.6 + 0.4 * ((x + y) % 4) / 3   # some relief
            colour = (118, 101, 73) if y < 8 else (49, 56, 47)
            pixels.append((*(round(c * shade) for c in colour), 40 + 13 * x))
    image.putdata(pixels)
    path = Path(folder) / "T_Alchemy_mortar_pestle.dds"
    image.save(path, pixel_format="DXT5")
    return path


def decoded(data, folder, name):
    path = Path(folder) / name
    path.write_bytes(data)
    return Image.open(path).convert("RGBA")


def average(image, rows):
    px = [image.getpixel((x, y)) for y in rows for x in range(16)]
    return [sum(p[i] for p in px) / len(px) for i in range(3)]


with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    stock_path = synthetic(temp)
    stock = stock_path.read_bytes()
    head = pipe.header_size(stock)
    if make:
        values = {"alchemyMetal": METAL, "alchemyStone": STONE}
        baked = make(stock_path, values, temp, None)
        check(len(baked) == len(stock) and baked[:head] == stock[:head], "the result keeps the stock size and header")
        check(all(baked[head + o:head + o + 8] == stock[head + o:head + o + 8] for o in range(0, len(stock) - head, 16)),
              "every stock BC3 alpha block is kept byte for byte")
        out = decoded(baked, temp, "out.dds")
        top, bottom = average(out, range(0, 8)), average(out, range(8, 16))
        check(top[2] > top[0] + 20, f"the bronze metal becomes the blue metal colour ({[round(c) for c in top]})")
        check(all(abs(a - b) <= 25 for a, b in zip(top, METAL)), f"the metal averages near alchemyMetal ({[round(c) for c in top]})")
        check(all(abs(a - b) <= 12 for a, b in zip(bottom, STONE)), f"the stone averages near alchemyStone ({[round(c) for c in bottom]})")
        lum = [sum(out.getpixel((x, 2))[:3]) for x in range(16)]
        check(max(lum) - min(lum) > 20, "the stock relief (shading) is kept in the metal")
        red = make(stock_path, {"alchemyMetal": [200.0, 40.0, 40.0], "alchemyStone": STONE}, temp, None)
        top_red, bottom_red = average(decoded(red, temp, "red.dds"), range(0, 8)), average(decoded(red, temp, "red.dds"), range(8, 16))
        check(top_red[0] > top_red[2] + 40, f"a red alchemyMetal turns the metal red ({[round(c) for c in top_red]})")
        check(all(abs(a - b) <= 3 for a, b in zip(bottom_red, bottom)), "alchemyMetal leaves the stone as it was")
        light = make(stock_path, {"alchemyMetal": METAL, "alchemyStone": [90.0, 90.0, 90.0]}, temp, None)
        check(average(decoded(light, temp, "light.dds"), range(8, 16))[1] > bottom[1] + 30, "alchemyStone changes the stone")
        check(make(stock_path, values, temp, None) == baked, "the recipe is deterministic")

    # ---- 4. the stock check in bake() ----
    check(getattr(pipe, "ALCHEMY_STOCK_SHA256", {}) == ALCHEMY, "the pipeline checks the three verified stock .uexp hashes")

    def bake(recipe_name, uexp, art=None):
        legacy, stage, work = temp / "legacy", temp / "stage", temp / "work"
        asset = "Art/UI/Modern/GameMenuLayer/Alchemy/T_Alchemy_mortar_pestle"
        src = legacy / "OblivionRemastered/Content" / asset
        src.parent.mkdir(parents=True, exist_ok=True)
        src.with_suffix(".uasset").write_bytes(b"uasset")
        src.with_suffix(".uexp").write_bytes(uexp)
        work.mkdir(exist_ok=True)

        def inject(uasset, dds, out):
            Path(out).parent.mkdir(parents=True, exist_ok=True)
            Path(out).write_bytes(b"injected")
            Path(out).with_suffix(".uexp").write_bytes(Path(dds).read_bytes()[:64])
        try:
            return pipe.bake(asset, recipe_name, custom_colours.pack_values(), art or temp, legacy, stage, work,
                             lambda name: stock_path, inject), None
        except pipe.SkipAsset as skip:
            return None, str(skip)
        except Exception as error:   # noqa: BLE001
            return None, f"{type(error).__name__}: {error}"

    result, reason = bake("alchemy", b"not the stock uexp")
    check(result is None and reason and "stock" in reason, f"a different stock asset is skipped, the reason names the stock ({reason})")
    saved = dict(getattr(pipe, "ALCHEMY_STOCK_SHA256", {}))
    if hasattr(pipe, "ALCHEMY_STOCK_SHA256"):
        pipe.ALCHEMY_STOCK_SHA256["T_Alchemy_mortar_pestle"] = hashlib.sha256(b"synthetic stock").hexdigest()
    result, reason = bake("alchemy", b"synthetic stock")
    if hasattr(pipe, "ALCHEMY_STOCK_SHA256"):
        pipe.ALCHEMY_STOCK_SHA256.clear()
        pipe.ALCHEMY_STOCK_SHA256.update(saved)
    check(result is not None, f"the verified stock asset is baked ({reason})")
    if result:
        check(result[0] == {"alchemyMetal": METAL, "alchemyStone": STONE} and result[1]["style"] == ["alchemyMetal", "alchemyStone"],
              "the manifest entry records both colours")
    empty = temp / "no-art"
    empty.mkdir()
    result, reason = bake("png", b"x", art=empty)
    check(result is None and reason and "art" in reason, "the png recipe (development only) still skips without owner art")

# ---- 5. a changed key makes the pack stale ----
style = {k: v for k, v in custom_colours.pack_values().items()}
with tempfile.TemporaryDirectory() as temp:
    ini_path = Path(temp) / "CustomColours.ini"
    ini_path.write_text(custom_colours.render(custom_colours.DEFAULTS | {"alchemyMetal": [200.0, 40.0, 40.0]}), encoding="utf-8")
    m = model.ColourModel(ini_path) if hasattr(model, "ColourModel") else model.Model(ini_path)
    check(m.needs_build(style) == ["alchemyMetal"], f"the builder flags a changed alchemyMetal as needing a build ({m.needs_build(style)})")
    check(pipe.recipe_values("alchemy", custom_colours.load(ini_path)).get("alchemyMetal") == [200.0, 40.0, 40.0]
          if make else False, "the recipe reads alchemyMetal from the colour file")

# ---- 6. 69 baked, 0 skipped, without owner art (fakes of custom_pack_speed_tests.py) ----
BC3_DDS = b"DDS " + bytes(8) + (4).to_bytes(4, "little") * 2 + bytes(64) + b"DXT5" + bytes(40) + bytes(16)


def write_asset(folder, asset):
    path = Path(folder) / "OblivionRemastered/Content" / asset
    path.parent.mkdir(parents=True, exist_ok=True)
    path.with_suffix(".uasset").write_bytes(b"uasset " + asset.encode())
    path.with_suffix(".uexp").write_bytes(b"uexp " + asset.encode() + bytes(4096))


def synthetic_hash(asset):
    return hashlib.sha256(b"uexp " + asset.encode() + bytes(4096)).hexdigest()


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


def fake_recipe(name):
    floats, rgbs, _ = REAL(name)
    return floats, rgbs, lambda data, values, work, stock: data.read_bytes() if hasattr(data, "read_bytes") else data


REAL = pipe.recipe
fake = Fake()
saved = {k: getattr(pipe, k, None) for k in ("run", "recipe", "dds_tools", "LAUREL_STOCK_SHA256", "ALCHEMY_STOCK_SHA256")}
pipe.run, pipe.recipe, pipe.dds_tools = fake.run, fake_recipe, (lambda path=None: (fake.export, fake.inject))
pipe.swap_imports = lambda uasset, swaps: uasset   # fake packages; the real header swap: popup_paper_swap_tests
laurel = next(a for a, r in pipe.TEXTURES.items() if r == "laurels")
pipe.LAUREL_STOCK_SHA256 = synthetic_hash(laurel)
pipe.ALCHEMY_STOCK_SHA256 = {Path(a).name: synthetic_hash(a) for a in pipe.TEXTURES if Path(a).name in ALCHEMY}
try:
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        retoc = temp / "retoc.exe"
        retoc.write_bytes(b"")
        (temp / "no-art").mkdir()
        with contextlib.redirect_stdout(io.StringIO()):
            manifest = pipe.build(SimpleNamespace(retoc=retoc, paks=temp / "Paks", ddstools=None), temp / "out",
                                  art=temp / "no-art")
        check(len(manifest["textures"]) == 191 and manifest["skipped"] == [],
              f"a build without owner art: 191 baked, 0 skipped ({len(manifest['textures'])}, {len(manifest['skipped'])} skipped)")
        check(manifest["style"].get("alchemyMetal") == METAL and manifest["style"].get("alchemyStone") == STONE,
              "the manifest style records alchemyMetal and alchemyStone")
except Exception as error:   # noqa: BLE001
    check(False, f"the faked build finishes ({type(error).__name__}: {error})")
finally:
    for k, v in saved.items():
        if v is None:
            if hasattr(pipe, k):
                delattr(pipe, k)
        else:
            setattr(pipe, k, v)

# ---- 7. offline first build: a clear message, the installed pack untouched ----
PACK = ("MRCustom_P.pak", "MRCustom_P.utoc", "MRCustom_P.ucas", "MRCustom_P.json")


def layout(base):
    paks = base / "game/OblivionRemastered/Content/Paks"
    paks.mkdir(parents=True)
    (paks / "OblivionRemastered-Windows.utoc").write_bytes(b"")
    target = base / "game/plugin/CustomPack"
    target.mkdir(parents=True)
    for f in PACK:
        (target / f).write_bytes(b"old " + f.encode())
    return paks, target


def snapshot(folder):
    return {p.relative_to(folder).as_posix(): p.read_bytes() for p in sorted(Path(folder).rglob("*")) if p.is_file()}


saved_run = subprocess.run
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    paks, target = layout(temp / "offline")
    retoc = temp / "offline/retoc/retoc.exe"   # writable folder, no Oodle DLL: retoc tries to download it
    retoc.parent.mkdir(parents=True)
    retoc.write_bytes(b"")
    before = snapshot(target.parent)

    def offline(cmd, *rest, **kw):
        return SimpleNamespace(returncode=1, stdout="", stderr="Error: error sending request for url (https://github.com/WorkingRobot/OodleUE/...)")

    subprocess.run = offline
    saved_tools = pipe.dds_tools
    pipe.dds_tools = lambda path=None: (None, None)   # the frozen exe bundles UE4-DDS-Tools: the build reaches retoc
    try:
        result = app.build_pack(paks, target, retoc, colours_path=temp / "none.ini", log=lambda line: None)
    finally:
        subprocess.run = saved_run
        pipe.dds_tools = saved_tools
    check(not result.ok and "internet" in result.message and "oo2core_9_win64.dll" in result.message,
          f"offline: the result explains the Oodle download needs internet ({result.message!r:.200})")
    check(snapshot(target.parent) == before, "offline: the installed pack and the plugin folder are unchanged")

    paks, target = layout(temp / "readonly")
    retoc = temp / "readonly/retoc/retoc.exe"
    retoc.parent.mkdir(parents=True)
    retoc.write_bytes(b"")
    before = snapshot(target.parent)
    saved_writable = app.paths.folder_writable
    app.paths.folder_writable = lambda folder: False   # a protected folder (Program Files / WindowsApps)
    calls = []
    subprocess.run = lambda *a, **k: calls.append(a) or SimpleNamespace(returncode=0, stdout="", stderr="")
    try:
        result = app.build_pack(paks, target, retoc, colours_path=temp / "none.ini", log=lambda line: None)
    finally:
        subprocess.run = saved_run
        app.paths.folder_writable = saved_writable
    check(not result.ok and "cannot be written" in result.message and str(retoc.parent) in result.message,
          f"read-only retoc folder: the message names the folder ({result.message!r:.200})")
    check(not calls, "read-only retoc folder: retoc is not started")
    check(snapshot(target.parent) == before, "read-only retoc folder: the installed pack is unchanged")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
