"""build_pack(): the one build the CLI (tools/make_dark_pack.py, its --check too) and the window call (design point 4).

Steps: check the game folder and retoc/Oodle, read the colour file, build into a temp folder next to the target, read the new
pack back with retoc, then replace the installed four files together (install.py). Never raises for a build problem: the
Result carries the message, so the window can show it.
"""
import configparser
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
    layout_manifest: dict = None   # MRLayout_P's manifest (None: not built, bLayoutPack=0 or its build failed)


def layout_enabled(colours_path):
    """[Layout] bLayoutPack of MenusRevamped.ini (OBSE/Plugins, the folder above the colour file's): 1 (default) = the sizes are
    baked (MRLayout_P, and MRCustom_P carries them too); 0 = no layout pack and a colour pack without sizes (decision 46)."""
    if not colours_path:
        return True
    ini = Path(colours_path).resolve().parent.parent / "MenusRevamped.ini"
    parser = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(";", "#"), strict=False)
    try:
        parser.read(ini, encoding="utf-8-sig")
        return parser.get("Layout", "bLayoutPack", fallback="1").strip() != "0"
    except (configparser.Error, UnicodeError, OSError):
        return True


def plugin_ini(colours_path):
    return Path(colours_path).resolve().parent.parent / "MenusRevamped.ini"


def set_preset(colours_path, preset):
    """[Style] uPreset in MenusRevamped.ini (decision 47: Vanilla colours = 0, a good Build = 2). Only that line changes; the
    file's comments, other keys, BOM and line endings are kept. A missing file or section is created."""
    ini = plugin_ini(colours_path)
    raw = ini.read_bytes() if ini.is_file() else b""
    bom = raw.startswith(b"\xef\xbb\xbf")
    text = raw.decode("utf-8-sig", errors="replace")
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.splitlines()
    section, done, style_at = None, False, None
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("[") and "]" in stripped:
            if section == "style" and not done:
                lines.insert(i, f"uPreset={preset}")
                done = True
                break
            section = stripped[1:stripped.index("]")].strip().lower()
            style_at = i if section == "style" else style_at
            continue
        if section == "style" and stripped.split("=", 1)[0].strip().lower() == "upreset" and "=" in stripped:
            lines[i] = f"uPreset={preset}"
            done = True
            break
    if not done:
        if style_at is None:
            lines += ["[Style]", f"uPreset={preset}"]
        else:
            lines.append(f"uPreset={preset}")
    ini.parent.mkdir(parents=True, exist_ok=True)
    ini.write_bytes((b"\xef\xbb\xbf" if bom else b"") + (newline.join(lines) + newline).encode("utf-8"))
    return ini


def summary(manifest, target, layout=None):
    built, skipped = len(manifest["textures"]), manifest["skipped"]
    lines = [f"{manifest['pack']} built: {built} asset(s) baked, {len(skipped)} skipped."]
    if layout:
        lines.append(f"{layout['pack']} built: {len(layout['textures'])} package(s) with the sizes, {len(layout['skipped'])} skipped.")
        skipped = skipped + layout["skipped"]
    lines.append(f"Installed in {target}. Start the game: the plugin installs the colour pack for the Custom preset (uPreset=2), "
                 f"otherwise the sizes pack.")
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
        sizes = custom_colours.load_sizes(colours_path) if colours_path else dict(custom_colours.SIZES)
        layout = layout_enabled(colours_path)
        target.parent.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix=".PackBuilder-", dir=target.parent))   # same drive: the replace is a rename
        out = work / "out"
        args = SimpleNamespace(paks=paks, retoc=Path(retoc), ddstools=ddstools)
        manifest = pipeline.build(args, out, pack, colours, art=art, log=log, sizes=sizes if layout else None)
        pipeline.readback(Path(retoc), out / f"{pack}.utoc", list(manifest["textures"]), log)
        layout_manifest, layout_problem = None, None
        if layout:   # a failing sizes pack does not stop the colours: the installed sizes pack stays as it was
            try:
                layout_manifest = pipeline.build_layout(args, out, sizes, log=log)
                pipeline.readback(Path(retoc), out / f"{pipeline.LAYOUT_PACK}.utoc", list(layout_manifest["textures"]), log)
            except pipeline.BuildError as stop:
                layout_manifest, layout_problem = None, str(stop.code)
                log(f"Sizes pack not built: {layout_problem}")
        files = install.install(out, target, pack)
        if layout_manifest:
            files += install.install(out, target, pipeline.LAYOUT_PACK)
    except SystemExit as stop:   # BuildError, and the colour file's own message for a broken value
        log(f"FAILED: {stop.code}")
        return Result(False, f"Build failed: {stop.code}")
    except Exception as error:   # a bug: the user still gets a message, the log gets the traceback
        log(traceback.format_exc())
        return Result(False, f"Build failed ({type(error).__name__}: {error}). The installed pack was not changed.")
    finally:
        if work is not None:
            shutil.rmtree(work, ignore_errors=True)
    message = summary(manifest, target, layout_manifest)
    if layout_problem:
        message += f"\nThe sizes pack ({pipeline.LAYOUT_PACK}) was not built: {layout_problem} The installed one was not changed."
    log(message)
    return Result(True, message, manifest, list(manifest["skipped"]), files, layout_manifest)
