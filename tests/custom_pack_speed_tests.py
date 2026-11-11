"""Custom pack builder Phase 2 (owner 2026-11-11): the same pack, built faster.
- every name the build needs (the data files' assets and the glyph masks) comes from ONE `retoc to-legacy` call with one -f each;
- UE4-DDS-Tools runs in-process (export and inject), so the build starts no process per texture: two retoc calls in total;
- an asset retoc did not extract is refused before any texture work, naming every missing asset;
- a missing retoc, a missing UE4-DDS-Tools or a failed Oodle download stops with a clear message.
retoc and the DDS tools are replaced by fakes here; `make_dark_pack.py --check` proves the real build."""
import contextlib
import hashlib
import importlib.util
import io
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
# Phase 4: the build moved into tools/pack_builder/pipeline.py (make_dark_pack.py is its CLI); the fakes patch the pipeline
sys.path.insert(0, str(root / "tools"))
import pack_builder.pipeline as mdp  # noqa: E402

BC3_DDS = b"DDS " + bytes(8) + (4).to_bytes(4, "little") * 2 + bytes(64) + b"DXT5" + bytes(40) + bytes(16)
MASK_DIR = "Art/UI/FakeMasks"   # where the fake retoc puts the glyph masks (they are listed by name only)
NEEDED = sorted({Path(a).name for a in mdp.TEXTURES} | {n for names in mdp.GLYPH_MASKS.values() for n in names}
                | {m for m, _ in getattr(mdp, "PANEL_MASKS", {}).values()})   # Phase 7: the Enchanting panel's paper mask
check(len(NEEDED) >= 70, f"the build needs the 69 assets and the glyph masks (found {len(NEEDED)} names)")


def write_asset(folder, asset):
    path = Path(folder) / "OblivionRemastered/Content" / asset
    path.parent.mkdir(parents=True, exist_ok=True)
    path.with_suffix(".uasset").write_bytes(b"uasset " + asset.encode())
    path.with_suffix(".uexp").write_bytes(b"uexp " + asset.encode() + bytes(4096))


class FakeTools:
    def __init__(self, omit=()):
        self.omit = set(omit)
        self.calls = []
        self.exports = []
        self.injects = []

    def run(self, cmd, *rest, **kw):
        cmd = [str(c) for c in cmd]
        self.calls.append(cmd)
        if cmd[1:2] == ["to-legacy"]:
            names = [cmd[i + 1] for i, c in enumerate(cmd) if c == "-f"]
            out = cmd[-1]
            for asset in mdp.TEXTURES:
                if Path(asset).name in names and Path(asset).name not in self.omit:
                    write_asset(out, asset)
            for masks in mdp.GLYPH_MASKS.values():
                for name in masks:
                    if name in names and name not in self.omit:
                        write_asset(out, f"{MASK_DIR}/{name}")
            return ""
        if cmd[1:2] == ["to-zen"]:
            utoc = Path(cmd[-1])
            utoc.parent.mkdir(parents=True, exist_ok=True)
            for ext in ("pak", "utoc", "ucas"):
                utoc.with_suffix("." + ext).write_bytes(b"pack")
            return ""
        if "--mode" in cmd:   # UE4-DDS-Tools as a subprocess (the old way): serve it so the build can finish
            mode, save = cmd[cmd.index("--mode") + 1], Path(cmd[cmd.index("--save_folder") + 1])
            name = Path(cmd[2]).stem
            save.mkdir(parents=True, exist_ok=True)
            if mode == "export":
                (save / (name + ".dds")).write_bytes(b"DDS")
            else:
                for ext in (".uasset", ".uexp"):
                    (save / (name + ext)).write_bytes(b"injected")
            return ""
        raise AssertionError(f"unexpected command {cmd}")

    def export(self, uasset, dds):
        self.exports.append(Path(uasset))
        Path(dds).parent.mkdir(parents=True, exist_ok=True)
        Path(dds).write_bytes(BC3_DDS)   # Phase 4 checks the stock texture is BC3 and the result keeps its size

    def inject(self, uasset, dds, out_uasset):
        self.injects.append(Path(uasset))
        out = Path(out_uasset)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(b"injected")
        out.with_suffix(".uexp").write_bytes(b"injected")


def fake_recipe(name):
    return (), (), lambda data, values, work, stock: data.read_bytes() if hasattr(data, "read_bytes") else data


def build_with(fake, retoc=None, ddstools=None):
    """build() with retoc, the DDS tools and the recipes replaced: (manifest or None, exit message or None)"""
    saved = {k: getattr(mdp, k, None) for k in ("run", "recipe", "dds_tools", "LAUREL_STOCK_SHA256", "ALCHEMY_STOCK_SHA256")}
    mdp.LAUREL_STOCK_SHA256 = "919642b09b2c79cc1c9732e96ac16ee07ee9d7d2d5cb196cdeb9b63347e2fca1"  # synthetic stock written by write_asset
    mdp.ALCHEMY_STOCK_SHA256 = {Path(a).name: hashlib.sha256(b"uexp " + a.encode() + bytes(4096)).hexdigest()
                                for a, r in mdp.TEXTURES.items() if r == "alchemy"}   # Phase 6: the alchemy stock check
    mdp.run = fake.run
    mdp.recipe = fake_recipe
    mdp.dds_tools = lambda path=None: (fake.export, fake.inject)
    try:
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            if retoc is None:
                retoc = temp / "retoc.exe"
                retoc.write_bytes(b"")
                (temp / "oo2core_9_win64.dll").write_bytes(b"")
            if ddstools is None:
                ddstools = temp / "ddstools/main.py"
                ddstools.parent.mkdir()
                ddstools.write_text("")
            args = SimpleNamespace(retoc=retoc, paks=temp / "Paks", ddstools=ddstools)
            with contextlib.redirect_stdout(io.StringIO()):
                return mdp.build(args, temp / "out"), None
    except SystemExit as error:
        return None, str(error.code)
    finally:
        for k, v in saved.items():
            if v is None:
                if hasattr(mdp, k):
                    delattr(mdp, k)
            else:
                setattr(mdp, k, v)


# 1. one to-legacy call carrying every needed name; to-zen once; no other process
fake = FakeTools()
manifest, error = build_with(fake)
check(error is None, f"the faked build finishes ({error})")
legacy = [c for c in fake.calls if c[1:2] == ["to-legacy"]]
zen = [c for c in fake.calls if c[1:2] == ["to-zen"]]
check(len(legacy) == 1, f"exactly one retoc to-legacy call (found {len(legacy)})")
if legacy:
    filters = [legacy[0][i + 1] for i, c in enumerate(legacy[0]) if c == "-f"]
    check(sorted(filters) == NEEDED, "the to-legacy call carries one -f per needed name (69 assets + glyph masks), nothing else")
    check(len(filters) == len(set(filters)), "no -f filter is repeated")
    check("--no-shaders" in legacy[0] and legacy[0][legacy[0].index("--version") + 1] == "UE5_3",
          "to-legacy keeps --no-shaders --version UE5_3")
check(len(zen) == 1, f"exactly one retoc to-zen call (found {len(zen)})")
check(len(fake.calls) == 2, f"the build starts two processes in total, both retoc (found {len(fake.calls)})")
check(not any(sys.executable in c or any(s.endswith("main.py") for s in c) for c in fake.calls),
      "no Python subprocess (UE4-DDS-Tools runs in-process)")

# 2. the DDS tools in-process: one export per texture (plus the masks when a recipe needs them), one inject per texture
textures = [a for a, r in mdp.TEXTURES.items() if not r.startswith(("rich_row:", "bp_colour:", "material_colour:"))]
check(len(fake.injects) == len(textures), f"one in-process inject per texture ({len(fake.injects)} of {len(textures)})")
check(len(fake.exports) == len(textures), f"one in-process export per texture ({len(fake.exports)} of {len(textures)})")
check(all(p.suffix == ".uasset" for p in fake.exports + fake.injects), "export and inject receive the .uasset")
if manifest:
    check(list(manifest["textures"]) == list(mdp.TEXTURES), "the manifest lists every asset in build order")
    check(set(manifest) == {"pack", "game_build", "built_by", "builder_version", "style", "textures", "skipped", "files"},
          "the manifest format is unchanged (Phase 4 adds builder_version and skipped)")
    check(all(e["baked_uexp_sha256"] for e in manifest["textures"].values()), "every asset has its baked hash")

# 3. an asset retoc did not extract is refused before any texture work, naming every missing name
texture_name = Path(textures[-1]).name
mask_name = next(iter(next(iter(mdp.GLYPH_MASKS.values()))))
fake = FakeTools(omit=(texture_name, mask_name))
manifest, error = build_with(fake)
check(manifest is None and error is not None, "a missing extracted asset stops the build")
check(error is not None and texture_name in error and mask_name in error,
      f"the message names every asset retoc did not extract ({error!r:.200})")
check(not fake.exports and not fake.injects, "nothing is exported or injected when an asset is missing")
check(len(fake.calls) == 1, "no further process after the refused extract")

# 4. clear messages for a missing retoc, a missing UE4-DDS-Tools and a failed Oodle download
with tempfile.TemporaryDirectory() as temp:
    missing = Path(temp) / "nowhere/retoc.exe"
    try:
        manifest, error = build_with(FakeTools(), retoc=missing)
    except Exception as crash:   # a traceback is not a clear message
        manifest, error = None, None
        check(False, f"a missing retoc gives a message, not a traceback ({type(crash).__name__})")
    else:
        check(error is not None and "retoc" in error and str(missing) in error, f"a missing retoc is named ({error!r:.200})")

dds_tools = getattr(mdp, "dds_tools", None)
check(callable(dds_tools), "make_dark_pack.dds_tools(path) loads UE4-DDS-Tools in-process")
if callable(dds_tools):
    with tempfile.TemporaryDirectory() as temp:
        bad = Path(temp) / "UE4-DDS-Tools/src/main.py"
        try:
            dds_tools(bad)
            check(False, "a missing UE4-DDS-Tools stops with a message")
        except SystemExit as stop:
            check("UE4-DDS-Tools" in str(stop.code) and str(bad.parent) in str(stop.code),
                  f"the message names UE4-DDS-Tools and the folder ({stop.code!r:.200})")
        except Exception as crash:
            check(False, f"a missing UE4-DDS-Tools gives a message, not a traceback ({type(crash).__name__})")

saved_subprocess_run = subprocess.run
with tempfile.TemporaryDirectory() as temp:
    retoc = Path(temp) / "retoc.exe"   # no oo2core_9_win64.dll next to it: retoc must download it
    retoc.write_bytes(b"")

    def offline(cmd, *rest, **kw):
        return SimpleNamespace(returncode=1, stdout="", stderr="Error: error sending request for url (https://github.com/...)")

    subprocess.run = offline
    try:
        try:
            mdp.run([retoc, "to-legacy", "--no-shaders", "--version", "UE5_3", "-f", "X", Path(temp), Path(temp) / "o"])
            check(False, "a failed retoc call stops")
        except SystemExit as stop:
            text = str(stop.code)
            check("Oodle" in text and "oo2core_9_win64.dll" in text and "internet" in text,
                  f"without the Oodle DLL a failed retoc call explains the download (internet) ({text!r:.200})")
    finally:
        subprocess.run = saved_subprocess_run

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
