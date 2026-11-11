"""Where the builder finds the game, retoc, its data files and the colour file.

Frozen (the exe in Plugins/MenusRevamped/PackBuilder): retoc/, menus/, art/ and PackBuilder.log next to the exe; the colour file
and CustomPack/ in the plugin folder above it; the game's Content/Paks found by walking up from the exe. From source: the
repo's own folders (tools/menus, assets-src/DarkPack, config/CustomColours.ini, assets/CustomPack).
"""
import sys
import tempfile
from pathlib import Path

from pack_builder import BuildError

TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parent
MARKER = "OblivionRemastered-Windows.utoc"   # the game's base container: proves a folder is the game's Paks
OODLE_DLL = "oo2core_9_win64.dll"
EXE_NAME = "MenusRevampedPackBuilder.exe"


def is_frozen():
    return bool(getattr(sys, "frozen", False))


def exe_dir():
    return Path(sys.executable).parent


def paks_in(folder):
    """The Paks folder for a folder that is Paks, OblivionRemastered or the game folder; None if it holds no game."""
    folder = Path(folder)
    for candidate in (folder, folder / "Content/Paks", folder / "OblivionRemastered/Content/Paks"):
        if (candidate / MARKER).is_file():
            return candidate
    return None


def find_paks(start):
    """The game's OblivionRemastered/Content/Paks above start (the exe sits in .../OBSE/Plugins/MenusRevamped/PackBuilder)."""
    start = Path(start).absolute()
    for folder in (start, *start.parents):
        found = paks_in(folder)
        if found:
            return found
    return None


def check_paks(folder):
    """A folder picked by hand (the game folder, OblivionRemastered or Paks) -> its Paks folder; a clear message otherwise."""
    found = paks_in(folder) if folder else None
    if found is None:
        raise BuildError(f"No {MARKER} in {folder}: choose the game folder (the one that holds OblivionRemastered) or its "
                         f"OblivionRemastered/Content/Paks folder.")
    return found


def defaults():
    """{paks, retoc, ddstools, menus, art, colours, out, log}: the builder's own places."""
    if is_frozen():
        here = exe_dir()
        plugin = here.parent
        return {"paks": find_paks(here), "retoc": here / "retoc/retoc.exe", "ddstools": None, "menus": here / "menus",
                "art": here / "art", "colours": plugin / "CustomColours.ini", "out": plugin / "CustomPack",
                "log": here / "PackBuilder.log"}
    return {"paks": None, "retoc": None, "ddstools": None, "menus": TOOLS / "menus", "art": ROOT / "assets-src/DarkPack",
            "colours": ROOT / "config/CustomColours.ini", "out": ROOT / "assets/CustomPack", "log": ROOT / "build/PackBuilder.log"}


def folder_writable(folder):
    try:
        with tempfile.TemporaryFile(dir=folder):
            return True
    except OSError:
        return False


def oodle_problem(retoc):
    """None when retoc can read the game's containers; otherwise the message (retoc missing, or no Oodle DLL and a folder
    retoc cannot download it into: owner decision, Phase 0)."""
    retoc = Path(retoc) if retoc else None
    if retoc is None or not retoc.is_file():
        return f"retoc not found: {retoc} (it ships in PackBuilder/retoc; --retoc: the path of retoc.exe)"
    if (retoc.parent / OODLE_DLL).is_file() or folder_writable(retoc.parent):
        return None
    return (f"retoc needs Oodle ({OODLE_DLL}) to read the game's containers. It downloads the DLL into {retoc.parent} on its "
            f"first run (internet needed), but that folder cannot be written. Allow writing there (or move the game out of a "
            f"protected folder), or copy an {OODLE_DLL} into {retoc.parent}.")
