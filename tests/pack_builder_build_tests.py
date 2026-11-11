"""Custom pack builder Phase 4 (owner 2026-11-11, design point 7 and owner decision 9): a build never leaves a half-written pack.
- each asset is checked before it is written (blueprint stock colour literals, BC3 format, same size as the stock texture,
  owner art present for the png recipe); a failing asset is skipped and listed in the manifest's "skipped", the rest is built;
- the manifest stays readable by the Phase 3 plugin (game_build, one top-level style object) and names the builder version;
- the build goes to a temp folder, retoc reads the new pack back (verify + every package listed), then the four files
  MRCustom_P.{pak,utoc,ucas,json} replace the installed ones together, the previous set kept as .bak;
- a failure anywhere (retoc, read-back, the replace itself) leaves the installed pack as it was; no temp folder is left;
- the builder never touches Content/Paks/~mods (the plugin copies the pack at the next start).
retoc and the DDS tools are fakes here (like custom_pack_speed_tests.py)."""
import contextlib
import importlib
import importlib.util
import io
import json
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
try:
    pipe = importlib.import_module("pack_builder.pipeline")
except Exception as error:   # before Phase 4 the build lives in make_dark_pack.py
    check(False, f"tools/pack_builder/pipeline.py imports ({type(error).__name__}: {error})")
    spec = importlib.util.spec_from_file_location("make_dark_pack", root / "tools/make_dark_pack.py")
    pipe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pipe)
try:
    app = importlib.import_module("pack_builder.app")
except Exception as error:
    app = None
    check(False, f"tools/pack_builder/app.py imports ({type(error).__name__}: {error})")
try:
    install_mod = importlib.import_module("pack_builder.install")
except Exception as error:
    install_mod = None
    check(False, f"tools/pack_builder/install.py imports ({type(error).__name__}: {error})")

PACK_FILES = ("MRCustom_P.pak", "MRCustom_P.utoc", "MRCustom_P.ucas", "MRCustom_P.json")
REAL_RECIPE = pipe.recipe
TEXTURES = pipe.TEXTURES
PNG = [a for a, r in TEXTURES.items() if r == "png"]
BLUEPRINTS = [a for a, r in TEXTURES.items() if r.startswith("bp_colour:")]
FLAT = [a for a, r in TEXTURES.items() if r.startswith("flat:")]
check(len(PNG) == 3, f"the data files hold the three owner-art (png) assets ({len(PNG)})")
BAD, GROWS = FLAT[0], FLAT[1]   # a texture that is not BC3, a texture whose recipe returns a different size
check(len(FLAT) >= 2, "two flat assets to break")


def dds(fourcc=b"DXT5", dxgi=None):
    head = b"DDS " + bytes(8) + (4).to_bytes(4, "little") + (4).to_bytes(4, "little") + bytes(64) + fourcc + bytes(40)
    if dxgi is not None:
        head += dxgi.to_bytes(4, "little") + bytes(16)
    return head + bytes(16)


def write_asset(folder, asset):
    path = Path(folder) / "OblivionRemastered/Content" / asset
    path.parent.mkdir(parents=True, exist_ok=True)
    path.with_suffix(".uasset").write_bytes(b"uasset " + asset.encode())
    path.with_suffix(".uexp").write_bytes(b"uexp " + asset.encode() + bytes(4096))


class FakeTools:
    def __init__(self, fail_zen=False, drop=None, fail_verify=False):
        self.fail_zen, self.drop, self.fail_verify = fail_zen, drop, fail_verify
        self.calls, self.injects, self.listing, self.zen = [], [], [], 0

    def stop(self, text):
        raise getattr(pipe, "BuildError", SystemExit)(text)

    def run(self, cmd, *rest, **kw):
        cmd = [str(c) for c in cmd]
        self.calls.append(cmd)
        if cmd[1:2] == ["to-legacy"]:
            names = {cmd[i + 1] for i, c in enumerate(cmd) if c == "-f"}
            for asset in TEXTURES:
                if Path(asset).name in names:
                    write_asset(cmd[-1], asset)
            for masks in pipe.GLYPH_MASKS.values():
                for name in masks:
                    write_asset(cmd[-1], f"Art/UI/FakeMasks/{name}")
            return ""
        if cmd[1:2] == ["to-zen"]:
            if self.fail_zen:
                self.stop(f"failed (1): {' '.join(cmd)}\nretoc crashed")
            stage = Path(cmd[-2])   # what the container holds (the build deletes its stage before the read-back)
            self.listing = [p.relative_to(stage).as_posix() for p in sorted(stage.rglob("*.uasset"))]
            self.zen += 1
            utoc = Path(cmd[-1])
            utoc.parent.mkdir(parents=True, exist_ok=True)
            for ext in ("pak", "utoc", "ucas"):
                utoc.with_suffix("." + ext).write_bytes(b"pack %d" % self.zen)
            return ""
        if cmd[1:2] == ["verify"]:
            if self.fail_verify:
                self.stop(f"failed (1): {' '.join(cmd)}\nbad container")
            return "verified\n"
        if cmd[1:2] == ["list"]:
            lines = []
            for rel in self.listing:
                if self.drop and rel.lower().endswith(Path(self.drop).name.lower() + ".uasset"):
                    continue
                lines.append(f"MRCustom_P  0000  ExportBundleData  ../../../{rel}")
            return "\n".join(lines + ["MRCustom_P  0001  ContainerHeader  -"]) + "\n"
        raise AssertionError(f"unexpected command {cmd}")

    def export(self, uasset, out):
        Path(out).parent.mkdir(parents=True, exist_ok=True)
        Path(out).write_bytes(dds(b"DX10", 71) if Path(uasset).stem == Path(BAD).name else dds())

    def inject(self, uasset, dds_path, out_uasset):
        self.injects.append(Path(uasset).stem)
        out = Path(out_uasset)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(b"injected")
        out.with_suffix(".uexp").write_bytes(b"injected " + Path(uasset).stem.encode())


def fake_recipe(name):
    kind = name.partition(":")[0]
    if kind in ("png", "bp_colour"):   # the real recipe: owner art lookup, blueprint stock check
        return REAL_RECIPE(name)
    floats, rgbs, _ = REAL_RECIPE(name)

    def make(data, values, work, stock):
        raw = data.read_bytes() if hasattr(data, "read_bytes") else data
        return raw + b"x" if hasattr(data, "stem") and data.stem == Path(GROWS).name else raw
    return floats, rgbs, make


@contextlib.contextmanager
def faked(fake, art):
    saved = {k: getattr(pipe, k) for k in ("run", "recipe", "dds_tools", "ART", "LAUREL_STOCK_SHA256")}
    pipe.LAUREL_STOCK_SHA256 = "919642b09b2c79cc1c9732e96ac16ee07ee9d7d2d5cb196cdeb9b63347e2fca1"  # synthetic stock written by write_asset
    pipe.run, pipe.recipe, pipe.ART = fake.run, fake_recipe, art
    pipe.dds_tools = lambda path=None: (fake.export, fake.inject)
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            yield
    finally:
        for k, v in saved.items():
            setattr(pipe, k, v)


with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    no_art = temp / "no-art"
    no_art.mkdir()
    retoc = temp / "retoc/retoc.exe"
    retoc.parent.mkdir()
    retoc.write_bytes(b"")
    (retoc.parent / "oo2core_9_win64.dll").write_bytes(b"")

    # ---- A. skip and list ----
    fake = FakeTools()
    try:
        with faked(fake, no_art):
            manifest = pipe.build(SimpleNamespace(retoc=retoc, paks=temp / "Paks", ddstools=temp / "dds"), temp / "outA")
        error = None
    except BaseException as stop:
        manifest, error = None, f"{type(stop).__name__}: {getattr(stop, 'code', stop)}"
    check(manifest is not None, f"a build with failing assets still finishes ({error!r:.200})")
    if manifest:
        skipped = {e.get("asset"): e.get("reason", "") for e in manifest.get("skipped", [])}
        check(set(PNG) <= set(skipped) and all("art" in skipped[a] for a in PNG),
              "a png asset whose owner art is missing is skipped, the reason names the art")
        check(set(BLUEPRINTS) <= set(skipped) and all("stock" in skipped[a] for a in BLUEPRINTS),
              "a blueprint whose stock colour literals differ is skipped, the reason names the stock value")
        check(BAD in skipped and "BC3" in skipped.get(BAD, ""), "a texture that is not BC3 is skipped")
        check(GROWS in skipped and "size" in skipped.get(GROWS, ""), "a recipe result of another size is skipped")
        expected = [a for a in TEXTURES if a not in set(PNG) | set(BLUEPRINTS) | {BAD, GROWS}]
        check(set(skipped) == set(TEXTURES) - set(expected), f"only the failing assets are skipped ({len(skipped)})")
        check(list(manifest.get("textures", {})) == expected, "every other asset is built, in build order")
        check(not any(Path(a).name in fake.injects for a in skipped), "nothing is injected for a skipped asset")
        check(all(e.get("recipe") for e in manifest.get("skipped", [])), "a skipped entry names its recipe")
        check(isinstance(manifest.get("builder_version"), str) and manifest.get("builder_version"),
              "the manifest names the builder version")
        check(isinstance(manifest.get("game_build"), str) and isinstance(manifest.get("style"), dict),
              "the manifest keeps game_build and the style object (the Phase 3 plugin reads them)")
        text = (temp / "outA/MRCustom_P.json").read_text(encoding="utf-8") if (temp / "outA/MRCustom_P.json").is_file() else ""
        check(text.count('"style": {') == 1, "exactly one '\"style\": {' in the manifest (the plugin finds the first)")

    # ---- B. the atomic replace ----
    def layout(name):
        game = temp / name
        paks = game / "OblivionRemastered/Content/Paks"
        (paks / "~mods").mkdir(parents=True)
        (paks / "OblivionRemastered-Windows.utoc").write_bytes(b"")
        (paks / "~mods/MRCustom_P.pak").write_bytes(b"installed")
        plugin = game / "OblivionRemastered/Binaries/Win64/OBSE/Plugins/MenusRevamped"
        (plugin / "CustomPack").mkdir(parents=True)
        (plugin / "PackBuilder").mkdir()
        for f in PACK_FILES:
            (plugin / "CustomPack" / f).write_bytes(b"old " + f.encode())
        return paks, plugin

    def snapshot(folder):
        return {p.relative_to(folder).as_posix(): p.read_bytes() for p in sorted(Path(folder).rglob("*")) if p.is_file()}

    def build_pack(fake, paks, plugin):
        try:
            with faked(fake, no_art):
                return app.build_pack(paks=paks, target=plugin / "CustomPack", retoc=retoc,
                                      colours_path=plugin / "CustomColours.ini", log=lambda line: None), None
        except BaseException as crash:
            return None, f"{type(crash).__name__}: {crash}"

    build_pack_fn = getattr(app, "build_pack", None)
    check(callable(build_pack_fn), "pack_builder.app has build_pack() (the one build the CLI and the window call)")
    if callable(build_pack_fn):
        paks, plugin = layout("B1")
        mods_before, names_before = snapshot(paks / "~mods"), sorted(p.name for p in plugin.iterdir())
        result, crash = build_pack(FakeTools(), paks, plugin)
        check(result is not None and result.ok, f"a good build succeeds ({crash or getattr(result, 'message', '')!r:.200})")
        pack = plugin / "CustomPack"
        check(all((pack / f).is_file() and not (pack / f).read_bytes().startswith(b"old") for f in PACK_FILES),
              "the four pack files are replaced")
        check(all((pack / (f + ".bak")).read_bytes() == b"old " + f.encode() for f in PACK_FILES if (pack / (f + ".bak")).is_file())
              and all((pack / (f + ".bak")).is_file() for f in PACK_FILES), "the previous set is kept as .bak")
        check(snapshot(paks / "~mods") == mods_before, "~mods is not touched")
        check(sorted(p.name for p in plugin.iterdir()) == names_before, "no temp folder is left in the plugin folder")
        check(result is not None and {e["asset"] for e in result.skipped} >= set(PNG), "the result lists the skipped assets")
        check(result is not None and isinstance(result.message, str) and result.message, "the result has a message for the window")
        first = snapshot(pack)
        result, crash = build_pack(FakeTools(), paks, plugin)
        check(result is not None and result.ok and all((pack / (f + ".bak")).read_bytes() == first[f] for f in PACK_FILES),
              "a second build keeps the first build's files as .bak")

        for label, fake, what in (("B2", FakeTools(fail_zen=True), "a failed retoc to-zen"),
                                  ("B3", FakeTools(drop=FLAT[-1]), "a read-back that misses a package"),
                                  ("B4", FakeTools(fail_verify=True), "a failed retoc verify")):
            paks, plugin = layout(label)
            before, names_before = snapshot(plugin), sorted(p.name for p in plugin.iterdir())
            result, crash = build_pack(fake, paks, plugin)
            check(result is not None and not result.ok and result.message,
                  f"{what} fails with a message, not an exception ({crash or getattr(result, 'message', '')!r:.160})")
            check(snapshot(plugin) == before, f"{what} leaves the installed pack as it was")
            check(sorted(p.name for p in plugin.iterdir()) == names_before, f"{what} leaves no temp folder")
            if label == "B3" and result is not None:
                check(Path(FLAT[-1]).name in result.message, "the read-back failure names the missing package")

    install = getattr(install_mod, "install", None)
    check(callable(install), "pack_builder.install has install(built, target, pack)")
    if callable(install):
        built, target = temp / "built", temp / "C/CustomPack"
        built.mkdir()
        target.mkdir(parents=True)
        for f in PACK_FILES:
            (built / f).write_bytes(b"new " + f.encode())
            (target / f).write_bytes(b"old " + f.encode())
            (target / (f + ".bak")).write_bytes(b"older " + f.encode())
        calls = []

        def flaky(src, dst):
            calls.append((src, dst))
            if len(calls) == 6:   # 4 moves to .bak, then the 2nd new file fails
                raise PermissionError("file in use")
            import os
            os.replace(src, dst)
        try:
            install(built, target, "MRCustom_P", replace=flaky)
            check(False, "a failed replace is reported")
        except BaseException as error:
            check(isinstance(error, getattr(pipe, "BuildError", ())), f"a failed replace raises BuildError ({type(error).__name__})")
        check(all((target / f).read_bytes() == b"old " + f.encode() for f in PACK_FILES),
              "a replace failing half-way puts the old set back (never a half-written pack)")
        check(not any((target / f).read_bytes().startswith(b"new") for f in PACK_FILES), "no new file stays installed")
        built2 = temp / "built2"
        built2.mkdir()
        (built2 / PACK_FILES[0]).write_bytes(b"x")
        try:
            install(built2, target, "MRCustom_P")
            check(False, "an incomplete new set is refused")
        except BaseException as error:
            check(isinstance(error, getattr(pipe, "BuildError", ())) and all((target / f).read_bytes() == b"old " + f.encode()
                                                                             for f in PACK_FILES),
                  "an incomplete new set is refused before anything is moved")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
