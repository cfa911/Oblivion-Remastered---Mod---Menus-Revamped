"""The files of the Menus Revamped plugin zip: {path in the zip: file in this checkout}. Release package scripts read this list.

Usage: python tools/plugin_files.py [source root]   (prints the list)

The Dark UI pack ships inside the plugin folder (Plugins/MenusRevamped/DarkPack); the plugin itself copies it into
Content/Paks/~mods at start-up when [Style] uPreset=2 and removes it otherwise (src/core/rules/DarkPackSwitch.h). Nothing is
shipped into ~mods directly: a Vortex-managed file there could not be removed by the Classic switch.
The build manifest (MRDark_P.json) stays in the source only. The README/notice files are added by the package script.
"""
import sys
from pathlib import Path

PREFIX = "OblivionRemastered/Binaries/Win64/OBSE/Plugins/"
PACK_FILES = ("MRDark_P.pak", "MRDark_P.utoc", "MRDark_P.ucas")


def entries(root, dll=None):
    root = Path(root)
    found = {
        PREFIX + "MenusRevamped.dll": Path(dll) if dll else root / "build/windows/x64/releasedbg/MenusRevamped.dll",
        PREFIX + "MenusRevamped.ini": root / "config/MenusRevamped.defaults.ini",
    }
    for png in sorted((root / "assets").rglob("*.png")):
        found[PREFIX + "MenusRevamped/" + png.relative_to(root / "assets").as_posix()] = png
    for name in PACK_FILES:
        found[PREFIX + "MenusRevamped/DarkPack/" + name] = root / "assets/DarkPack" / name
    return found


if __name__ == "__main__":
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    for name, path in entries(source).items():
        print(f"{name} <- {path}{'' if path.is_file() else '  (MISSING)'}")
