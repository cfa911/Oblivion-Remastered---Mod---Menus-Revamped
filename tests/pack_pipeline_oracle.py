"""The Python Pack Builder as the oracle for the C++ build pipeline (in-game Pack Builder Phase 3).

Runs tools/pack_builder/pipeline.build (and build_layout) with the given colour file into <out>, keeping the work folder
(<out>/work: legacy = retoc's stock extraction, stage = the baked packages, dds = the stock DDS exports, recoloured = the
recipe results), so a parity test can compare the C++ output with every staged file byte for byte.

  python -I tests/pack_pipeline_oracle.py --paks <game>/OblivionRemastered/Content/Paks --retoc <retoc.exe>
      --ddstools <UE4-DDS-Tools/src> --out <folder> [--colours <CustomColours.ini>] [--no-layout]
"""
import argparse
import contextlib
import shutil
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import custom_colours  # noqa: E402
from pack_builder import pipeline  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--paks", type=Path, required=True)
    parser.add_argument("--retoc", type=Path, required=True)
    parser.add_argument("--ddstools", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--colours", type=Path)
    parser.add_argument("--no-layout", action="store_true")
    args = parser.parse_args()
    out = args.out.resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    runs = iter(("work", "work-layout"))

    @contextlib.contextmanager
    def kept(*_, **__):   # the pipeline's temp folders, kept for the parity comparison
        folder = out / next(runs)
        folder.mkdir()
        yield str(folder)

    tempfile.TemporaryDirectory = kept
    colours = custom_colours.pack_values(args.colours) if args.colours else None
    sizes = None if args.no_layout else (custom_colours.load_sizes(args.colours) if args.colours else dict(custom_colours.SIZES))
    ns = SimpleNamespace(paks=args.paks, retoc=args.retoc, ddstools=args.ddstools)
    log = (out / "build.log").open("w", encoding="utf-8")
    pipeline.build(ns, out / "pack", colours=colours, log=lambda line: print(line, file=log), sizes=sizes)
    if sizes is not None:
        pipeline.build_layout(ns, out / "pack", sizes, log=lambda line: print(line, file=log))
    log.close()
    print(f"oracle written to {out}")


if __name__ == "__main__":
    main()
