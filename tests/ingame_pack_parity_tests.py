"""In-game Pack Builder Phase 3: the C++ build pipeline (src/builder/PackBuild.h) bakes the same files as the Python builder
(tools/pack_builder/pipeline.py), byte for byte, from the same stock assets of the installed game.

retoc extracts the stock assets once (pipeline.extract_all); then for each colour scenario (the Dark defaults, seeded random
colour files, extremes) the C++ driver (tests/ingame_pack_parity_driver.cpp) and pipeline.bake each bake every asset whose
recipe the C++ side has ported; every staged .uasset/.uexp/.ubulk must be identical, the manifest entries equal, the same
assets skipped, and the colour values equal (custom_colours.pack_values). Recipes of later phases are counted, not compared.

Needs the game and the tools (no personal paths in the repository):
  MR_PAKS      <game>/OblivionRemastered/Content/Paks
  MR_RETOC     retoc.exe (with its Oodle DLL)
  MR_DDSTOOLS  UE4-DDS-Tools src folder
cl on PATH (run_suites.py --msvc) or MR_MSVC_SETUP. Optional MR_PARITY_WORK: a work folder to keep (default: a temp folder).
"""
import gc
import json
import os
import random
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace

import source_paths

root = source_paths.ROOT
sys.path.insert(0, str(root / "tools"))
import custom_colours as cc  # noqa: E402
from pack_builder import pipeline  # noqa: E402

fails, count = [], 0


def check(ok, why):
    global count
    count += 1
    if not ok:
        fails.append(why)


def finish():
    for f in fails[:40]:
        print("FAIL", f)
    print(f"{count} checks, {len(fails)} failures")
    sys.exit(1 if fails else 0)


def compile_driver(work):
    exe = work / "driver.exe"
    src = root / "tests/ingame_pack_parity_driver.cpp"
    cmd = f'cl /nologo /std:c++latest /O2 /EHsc /utf-8 /I"{root / "src"}" "{src}" /Fe"{exe}" /Fo"{work}\\\\"'
    if not shutil.which("cl"):
        setup = os.environ.get("MR_MSVC_SETUP")
        if not setup:
            check(False, "no cl on PATH and no MR_MSVC_SETUP")
            finish()
        cmd = f'call "{setup}" >nul 2>nul && {cmd}'
    r = subprocess.run(f'cmd /d /s /c "{cmd}"', capture_output=True, text=True, errors="replace")
    if r.returncode or not exe.is_file():
        check(False, "driver compile failed: " + (r.stdout + r.stderr)[-800:])
        finish()
    return exe


def scenario_files(work):
    """{name: colour file or None}: the Dark defaults, two seeded random files and the extremes."""
    out = {"default": None}
    skin4 = [n for n in pipeline.custom_colours.BAKED_SKIN if isinstance(cc.SKIN[n], list)]
    skin1 = [n for n in pipeline.custom_colours.BAKED_SKIN if not isinstance(cc.SKIN[n], list)]
    for seed in (1, 2):
        rnd = random.Random(seed)
        lines = ["[Colours]"]
        lines += [f"{k} = " + ", ".join(str(round(rnd.uniform(0, 255), rnd.choice((0, 0, 2)))) for _ in range(3)) for k in cc.COLOURS]
        lines += [f"{k} = {round(rnd.uniform(0, 1), 3)}" for k in cc.NUMBERS]
        lines += ["[Tab]"] + [f"{k} = {rnd.randrange(256)}" for k in cc.TAB]
        lines += ["[Skin]"] + [f"{k} = " + ", ".join(str(round(rnd.uniform(0, 1), 4)) for _ in range(4)) for k in skin4]
        lines += [f"{k} = {round(rnd.uniform(0, 1), 4)}" for k in skin1]
        path = work / f"seed{seed}.ini"
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        out[f"seed{seed}"] = path
    lines = ["[Colours]"] + [f"{k} = " + ("0, 0, 0" if i % 2 else "255, 255, 255") for i, k in enumerate(cc.COLOURS)]
    lines += ["[Skin]"] + [f"{k} = 1, 1, 1, 0" for k in skin4] + [f"{k} = 0" for k in skin1]
    path = work / "extremes.ini"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    out["extremes"] = path
    return out


def main():
    env = {k: os.environ.get(k) for k in ("MR_PAKS", "MR_RETOC", "MR_DDSTOOLS")}
    missing = [k for k, v in env.items() if not v]
    if missing:
        check(False, f"set {', '.join(missing)} (see the docstring): the parity test needs the game and the tools")
        finish()
    keep = os.environ.get("MR_PARITY_WORK")
    work = Path(keep) if keep else Path(tempfile.mkdtemp(prefix="mr-pack-parity-"))
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    try:
        exe = compile_driver(work)
        legacy = work / "legacy"
        args = SimpleNamespace(paks=Path(env["MR_PAKS"]), retoc=Path(env["MR_RETOC"]), ddstools=Path(env["MR_DDSTOOLS"]))
        found = pipeline.extract_all(args, legacy, colours=True, layout=False)
        export, inject = pipeline.dds_tools(args.ddstools)
        exported = {}

        def stock_dds(name):
            if name not in exported:
                uasset = found[name.lower()][0]
                exported[name] = work / "dds" / (uasset.stem + ".dds")
                export(uasset, exported[name])
            return exported[name]

        totals = {"baked": 0, "skipped": 0, "later": 0}
        for name, ini in scenario_files(work).items():
            out = work / name
            r = subprocess.run([str(exe), str(pipeline.MENUS), str(legacy), str(ini) if ini else "-", str(out)],
                               capture_output=True, text=True, errors="replace")
            if r.returncode:
                check(False, f"{name}: driver failed {r.returncode}: {r.stderr[-400:]}")
                continue
            report = json.loads((out / "report.json").read_text(encoding="utf-8"))
            colours = cc.pack_values(ini) if ini else cc.pack_values()
            check(set(report["values"]) == set(colours), f"{name}: the same value keys "
                  f"(C++ only {sorted(set(report['values']) - set(colours))}, Python only {sorted(set(colours) - set(report['values']))})")
            for key, value in colours.items():
                check(report["values"].get(key) == value, f"{name}: value {key} C++ {report['values'].get(key)} Python {value}")
            stage, scratch = work / f"{name}-py/stage", work / f"{name}-py/work"
            for row in report["assets"]:
                asset, recipe = row["asset"], row["recipe"]
                totals[row["result"]] += 1
                if row["result"] == "later":
                    continue
                try:
                    _, entry = pipeline.bake(asset, recipe, colours, pipeline.ART, legacy, stage, scratch, stock_dds, inject, None)
                    py_result, py_reason = "baked", ""
                except Exception as error:
                    gc.collect()
                    py_result = "skipped"
                    py_reason = str(error) if isinstance(error, pipeline.SkipAsset) else f"{type(error).__name__}: {error}"
                    entry = None
                check(row["result"] == py_result, f"{name}: {asset} ({recipe}) C++ {row['result']} ({row['reason']}), Python {py_result} ({py_reason})")
                if py_result != "baked" or row["result"] != "baked":
                    continue
                check(row["entry"] == entry, f"{name}: {asset} manifest entry differs: C++ {row['entry']} Python {entry}")
                for ext in (".uasset", ".uexp", ".ubulk"):
                    py = (stage / "OblivionRemastered/Content" / asset).with_suffix(ext)
                    cpp = (out / "stage/OblivionRemastered/Content" / asset).with_suffix(ext)
                    if not py.is_file() and not cpp.is_file():
                        continue
                    same = py.is_file() and cpp.is_file() and py.read_bytes() == cpp.read_bytes()
                    if not same and py.is_file() and cpp.is_file():
                        a, b = py.read_bytes(), cpp.read_bytes()
                        first = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
                        why = f"first difference at byte {first} of {len(a)}/{len(b)}"
                    else:
                        why = "missing on one side"
                    check(same, f"{name}: {asset}{ext} ({recipe}) differs: {why}")
        print(f"assets per scenario run: baked {totals['baked']}, skipped {totals['skipped']}, later phases {totals['later']}")
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)
    finish()


main()
