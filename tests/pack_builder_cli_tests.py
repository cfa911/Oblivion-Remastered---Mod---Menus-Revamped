"""Custom pack builder Phase 4 (owner 2026-11-11, design point 4): one code path for the CLI and the window.
- the build lives in tools/pack_builder (recipes and pipeline moved out of make_dark_pack.py);
- make_dark_pack.py keeps its options and --check, and both a build and --check go through pack_builder.app.build_pack;
- the window's Build saves the colour file, then calls the same build_pack with that file; the window code starts no
  process and holds no recipe (it stays thin)."""
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


def module(name):
    try:
        return importlib.import_module(name)
    except Exception as error:
        check(False, f"{name} imports ({type(error).__name__}: {error})")
        return None


app = module("pack_builder.app")
window = module("pack_builder.window")
spec = importlib.util.spec_from_file_location("make_dark_pack", root / "tools/make_dark_pack.py")
mdp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mdp)

tool_text = (root / "tools/make_dark_pack.py").read_text(encoding="utf-8")
check("def flat_blocks" not in tool_text and "def build(" not in tool_text,
      "make_dark_pack.py holds no recipe and no build of its own (they live in tools/pack_builder)")
check("pack_builder" in tool_text, "make_dark_pack.py calls the pack_builder package")
for option in ("--paks", "--ddstools", "--retoc", "--out", "--pack", "--colours", "--check"):
    check(option in tool_text, f"make_dark_pack.py keeps {option}")
window_file = root / "tools/pack_builder/window.py"
window_text = window_file.read_text(encoding="utf-8") if window_file.is_file() else ""
check(bool(window_text) and not any(s in window_text for s in ("to-zen", "to-legacy", "subprocess", "def flat_blocks")),
      "the window code starts no process and holds no recipe")

calls = []


def recorder(manifest):
    def build_pack(**kw):
        calls.append(kw)
        return SimpleNamespace(ok=True, message="built", manifest=dict(manifest), skipped=[])
    return build_pack


main = getattr(mdp, "main", None)
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    out = temp / "CustomPack"
    out.mkdir()
    current = {"pack": "MRCustom_P", "game_build": "1.512.105.0", "built_by": "tools/make_dark_pack.py",
               "style": {"rowText": [239.0, 239.0, 239.0]}, "textures": {"A": {"recipe": "flat:rowText"}}, "files": {}}
    (out / "MRCustom_P.json").write_text(json.dumps(current), encoding="utf-8")
    fresh = dict(current, built_by="MenusRevampedPackBuilder.exe", builder_version="0.4.0", skipped=[],
                 files={"MRCustom_P.pak": "0"})
    argv = ["--paks", str(temp / "Paks"), "--retoc", str(temp / "retoc.exe"), "--ddstools", str(temp / "dds"),
            "--out", str(out)]
    if app is not None and hasattr(app, "build_pack"):
        saved = app.build_pack
        app.build_pack = recorder(fresh)
        try:
            for label, extra in (("build", []), ("--check", ["--check"])):
                calls.clear()
                code = None
                try:
                    with contextlib.redirect_stdout(io.StringIO()):
                        main(argv + extra)
                except SystemExit as stop:
                    code = stop.code
                except BaseException as crash:
                    code = f"CRASH {type(crash).__name__}: {crash}"
                check(len(calls) == 1, f"make_dark_pack.py {label} calls pack_builder.app.build_pack once ({len(calls)}, {code!r})")
                if calls and label == "build":
                    check(Path(calls[0].get("target", "")) == out, "a build targets --out")
                if calls and label == "--check":
                    check(Path(calls[0].get("target", out)) != out, "--check builds elsewhere (a temp folder), never into --out")
                    check(code in (0, None), f"--check: same baked textures when only built_by/builder_version/skipped=[] differ ({code!r})")
            changed = dict(fresh, style={"rowText": [255.0, 0.0, 0.0]})
            app.build_pack = recorder(changed)
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    main(argv + ["--check"])
                code = None
            except SystemExit as stop:
                code = stop.code
            check(code == 1, f"--check still reports a stale pack ({code!r})")

            Controller = getattr(window, "Controller", None)
            check(Controller is not None, "pack_builder.window has Controller (the window's logic, no Tk)")
            if Controller:
                app.build_pack = recorder(fresh)
                calls.clear()
                colours = temp / "Plugins/MenusRevamped/CustomColours.ini"
                c = Controller(paks=temp / "Paks", target=out, retoc=temp / "retoc.exe", colours_path=colours,
                               ddstools=None, art=None, log_path=temp / "PackBuilder.log")
                c.model.set_text("Colours", "rowText", "255, 80, 80")
                result = c.build(log=lambda line: None)
                check(len(calls) == 1, "the window's Build calls the same pack_builder.app.build_pack")
                check(calls and Path(calls[0].get("colours_path", "")) == colours and Path(calls[0].get("target", "")) == out,
                      "the window builds from its colour file into the pack folder")
                check(colours.is_file() and "rowText = 255, 80, 80" in colours.read_text(encoding="utf-8"),
                      "Build saves the colour file first (the plugin compares the pack with it)")
                check(getattr(result, "ok", False), "the window gets build_pack's result")
        finally:
            app.build_pack = saved

# owner decision 10: the development zip ships the frozen builder in Plugins/MenusRevamped/PackBuilder and the default pack
spec = importlib.util.spec_from_file_location("plugin_files", root / "tools/plugin_files.py")
plugin_files = importlib.util.module_from_spec(spec)
spec.loader.exec_module(plugin_files)
with tempfile.TemporaryDirectory() as temp:
    fake = Path(temp)
    dist = fake / "build/pack_builder/dist/PackBuilder"
    for rel in ("MenusRevampedPackBuilder.exe", "_internal/python315.dll", "menus/alchemy.json", "retoc/retoc.exe",
                "retoc/LICENSE", "retoc/oo2core_9_win64.dll", "licences/retoc-LICENSE.txt", "README.txt", "PackBuilder.log"):
        (dist / rel).parent.mkdir(parents=True, exist_ok=True)
        (dist / rel).write_bytes(b"x")
    names = set(plugin_files.entries(fake))
    prefix = plugin_files.PREFIX + "MenusRevamped/PackBuilder/"
    check({prefix + r for r in ("MenusRevampedPackBuilder.exe", "_internal/python315.dll", "menus/alchemy.json",
                                "retoc/retoc.exe", "retoc/LICENSE", "licences/retoc-LICENSE.txt", "README.txt")} <= names,
          "the zip ships the frozen builder from build/pack_builder/dist/PackBuilder")
    check(not any("oo2core" in n.lower() for n in names), "the zip never ships Oodle (a DLL retoc downloaded)")
    check(not any(n.endswith(".log") for n in names), "the zip ships no build log")
    check(not any(n.endswith("CustomColours.ini") for n in names), "the zip ships no CustomColours.ini (owner decision 8)")
    check(any(n.endswith("CustomPack/MRCustom_P.ucas") for n in plugin_files.entries(root)),
          "the zip still ships the default pack (owner decision 10)")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
