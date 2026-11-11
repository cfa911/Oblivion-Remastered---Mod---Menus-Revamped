"""The files of the Menus Revamped plugin zip: {path in the zip: file in this checkout}. Release package scripts read this list.

Usage: python tools/plugin_files.py [source root]   (prints the list)

The Custom UI pack ships inside the plugin folder (Plugins/MenusRevamped/CustomPack, with its manifest MRCustom_P.json); the
plugin itself copies the pack into Content/Paks/~mods at start-up when [Style] uPreset=2 (after checking the manifest's game
build) and removes it otherwise (src/core/rules/DarkPackSwitch.h). Nothing is shipped into ~mods directly: a Vortex-managed
file there could not be removed by the Classic switch. CustomColours.ini is NOT shipped: the plugin writes the default file
when it is missing, so a mod update never replaces the user's colours. The README/notice files are added by the package script.
The Custom pack builder (Phase 4, owner decision 10) ships in Plugins/MenusRevamped/PackBuilder from
build/pack_builder/dist/PackBuilder (python tools/build_pack_builder.py); a DLL retoc downloaded there (Oodle, proprietary)
and a build log are never shipped.
"""
import sys
from pathlib import Path

PREFIX = "OblivionRemastered/Binaries/Win64/OBSE/Plugins/"
PACK_FILES = ("MRCustom_P.pak", "MRCustom_P.utoc", "MRCustom_P.ucas", "MRCustom_P.json")
BUILDER = "build/pack_builder/dist/PackBuilder"


def shipped_builder_file(rel):
    name = rel.rsplit("/", 1)[-1].lower()
    return not name.startswith("oo2core") and not name.endswith((".log", ".bak"))


def entries(root, dll=None):
    root = Path(root)
    found = {
        PREFIX + "MenusRevamped.dll": Path(dll) if dll else root / "build/windows/x64/releasedbg/MenusRevamped.dll",
        PREFIX + "MenusRevamped.ini": root / "config/MenusRevamped.defaults.ini",
    }
    for png in sorted((root / "assets").rglob("*.png")):
        found[PREFIX + "MenusRevamped/" + png.relative_to(root / "assets").as_posix()] = png
    for name in PACK_FILES:
        found[PREFIX + "MenusRevamped/CustomPack/" + name] = root / "assets/CustomPack" / name
    builder = root / BUILDER
    for path in sorted(p for p in builder.rglob("*") if p.is_file()):
        rel = path.relative_to(builder).as_posix()
        if shipped_builder_file(rel):
            found[PREFIX + "MenusRevamped/PackBuilder/" + rel] = path
    return found


if __name__ == "__main__":
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    for name, path in entries(source).items():
        print(f"{name} <- {path}{'' if path.is_file() else '  (MISSING)'}")
