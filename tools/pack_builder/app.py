"""build_pack(): the one build the CLI (tools/make_dark_pack.py, its --check too) and the window call (design point 4).

Steps: check the game folder and retoc/Oodle, read the colour file, build into a temp folder next to the target, read the new
pack back with retoc, then replace the installed four files together (install.py). Never raises for a build problem: the
Result carries the message, so the window can show it.
"""
import shutil
import tempfile
import traceback
from dataclasses import dataclass, field
from pathlib import Path
from types import SimpleNamespace

import custom_colours
from pack_builder import install, paths, pipeline


@dataclass
class Result:
    ok: bool
    message: str
    manifest: dict = None
    skipped: list = field(default_factory=list)
    files: list = field(default_factory=list)


def summary(manifest, target):
    built, skipped = len(manifest["textures"]), manifest["skipped"]
    lines = [f"{manifest['pack']} built: {built} asset(s) baked, {len(skipped)} skipped.",
             f"Installed in {target}. Start the game with the Custom preset (uPreset=2): the plugin installs the new pack."]
    if skipped:
        lines.append("Skipped (the game's stock look shows for these):")
        lines += [f"  {Path(e['asset']).name}: {e['reason']}" for e in skipped]
    return "\n".join(lines)


def build_pack(paks, target, retoc, colours_path=None, ddstools=None, art=None, pack=pipeline.DEFAULT_PACK, log=None):
    """Builds <pack> from the colour file and installs it in target (the plugin's CustomPack folder). Returns a Result."""
    log = log or print
    target = Path(target)
    work = None
    try:
        paks = paths.check_paks(paks)
        problem = paths.oodle_problem(retoc)
        if problem:
            raise pipeline.BuildError(problem)
        colours = custom_colours.pack_values(colours_path)
        target.parent.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix=".PackBuilder-", dir=target.parent))   # same drive: the replace is a rename
        out = work / "out"
        args = SimpleNamespace(paks=paks, retoc=Path(retoc), ddstools=ddstools)
        manifest = pipeline.build(args, out, pack, colours, art=art, log=log)
        pipeline.readback(Path(retoc), out / f"{pack}.utoc", list(manifest["textures"]), log)
        files = install.install(out, target, pack)
    except SystemExit as stop:   # BuildError, and the colour file's own message for a broken value
        log(f"FAILED: {stop.code}")
        return Result(False, f"Build failed: {stop.code}")
    except Exception as error:   # a bug: the user still gets a message, the log gets the traceback
        log(traceback.format_exc())
        return Result(False, f"Build failed ({type(error).__name__}: {error}). The installed pack was not changed.")
    finally:
        if work is not None:
            shutil.rmtree(work, ignore_errors=True)
    message = summary(manifest, target)
    log(message)
    return Result(True, message, manifest, list(manifest["skipped"]), files)
