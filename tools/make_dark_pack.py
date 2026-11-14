"""Build the colour pack (default assets/CustomPack/MRCustom_P.pak/.utoc/.ucas): stock textures re-coloured from the colour file.

Usage:
  python tools/make_dark_pack.py --paks <game>/OblivionRemastered/Content/Paks --ddstools <UE4-DDS-Tools/src/main.py>
                                 --retoc <retoc.exe> [--out <dir>] [--pack <name>_P] [--colours <ini>] [--art <dir>] [--check]
  MenusRevampedPackBuilder.exe [the same options]   (the frozen builder: --paks, --retoc and --ddstools default to its own)

The CLI of the Custom pack builder: the recipes and the pipeline live in tools/pack_builder (pipeline.py), and a build and
--check both go through pack_builder.app.build_pack, the same function the builder window calls (design point 4). What is
baked lives in tools/menus/<menu>.json; the colours come from CustomColours.ini (default config/CustomColours.ini, generated
from src/core/DarkStyle.h by tools/custom_colours.py; a missing file or entry = today's Dark value). retoc reads the game's
containers (it downloads Oodle, oo2core_9_win64.dll, next to itself on its first run: internet) and writes the pack, which is
read back (retoc verify + list) before the four files <pack>.pak/.utoc/.ucas/.json replace the ones in --out together (the
previous set kept as .bak). An asset that fails its stock check is skipped and listed in the manifest's "skipped".

--check builds into a temp folder and compares the baked textures with --out's manifest (retoc orders a many-package container
differently per run, so the container bytes can differ). --art: the owner's edited textures for the png recipe (default
assets-src/DarkPack from source; the frozen builder has none, so those three Alchemy assets are skipped unless --art is given).
"""
import argparse
import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import custom_colours  # noqa: E402
from pack_builder import app, paths, pipeline  # noqa: E402
from pack_builder.pipeline import *  # noqa: E402,F401,F403  (the recipes and tables, for the tests and old callers)
from pack_builder.pipeline import DEFAULT_PACK, pack_name, sha256  # noqa: E402

COMPARED = ("pack", "game_build", "style", "textures", "sizes", "header_layout", "header_overflow")


def comparable(manifest):
    return {**{k: manifest.get(k) for k in COMPARED}, "skipped": manifest.get("skipped", [])}


def main(argv=None):
    d = paths.defaults()
    out_default = d["out"] if paths.is_frozen() else ROOT / "assets/CustomPack"
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--paks", type=Path, default=d["paks"], help="the game's OblivionRemastered/Content/Paks")
    parser.add_argument("--ddstools", type=Path, default=d["ddstools"], help="UE4-DDS-Tools src/main.py (the exe has its own)")
    parser.add_argument("--retoc", type=Path, default=d["retoc"], help="retoc.exe (the exe uses PackBuilder/retoc)")
    parser.add_argument("--out", type=Path, default=out_default)
    parser.add_argument("--pack", default=DEFAULT_PACK, help=f"pack file name (default {DEFAULT_PACK})")
    parser.add_argument("--colours", type=Path, default=d["colours"],
                        help="the colour file (default config/CustomColours.ini; missing file or entry = the Dark colours)")
    parser.add_argument("--art", type=Path, default=d["art"], help="the owner's edited textures (png recipe)")
    parser.add_argument("--check", action="store_true", help="build into a temp folder and compare with --out")
    args = parser.parse_args(argv)
    if args.paks is None:
        parser.error("--paks: the game's OblivionRemastered/Content/Paks (not found from the builder's folder)")
    if args.retoc is None:
        parser.error("--retoc: the path of retoc.exe")
    pack = pack_name(args.pack)
    common = dict(paks=args.paks, retoc=args.retoc, colours_path=args.colours, ddstools=args.ddstools, art=args.art,
                  pack=pack, log=print)
    if not args.check:
        result = app.build_pack(target=args.out, **common)
        print(result.message)
        sys.exit(0 if result.ok else 1)
    with tempfile.TemporaryDirectory() as temp:
        result = app.build_pack(target=Path(temp) / "CustomPack", **common)
    if not result.ok:
        print(result.message)
        sys.exit(1)
    layout = getattr(result, "layout_manifest", None)
    built = [(pack, result.manifest)] + ([(pipeline.LAYOUT_PACK, layout)] if layout else [])
    same = True
    for name, manifest in built:   # the colour pack and, with bLayoutPack=1, the sizes pack (Step B)
        try:
            current = json.loads((args.out / f"{name}.json").read_text(encoding="utf-8"))
        except (OSError, ValueError):
            current = {}
        files = current.get("files", {})
        ok = bool(current) and comparable(manifest) == comparable(current) and all(
            (args.out / n).is_file() and sha256(args.out / n) == digest for n, digest in files.items())
        print(f"{name}: " + ("up to date (same baked assets)" if ok else "STALE"))
        same = same and ok
    print("pack is up to date (same baked textures)" if same else "pack is STALE: run without --check to rebuild")
    sys.exit(0 if same else 1)


if __name__ == "__main__":
    main()
