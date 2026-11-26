"""In-game Pack Builder Phase 3d: the C++ build pipeline bakes the sizes like the Python builder, byte for byte, from the same
stock assets of the installed game (src/builder/PackLayout.h, PackBuild.h; tools/pack_builder/pipeline.py).

retoc extracts the colour and layout packages once (pipeline.extract_all); then for each scenario (today's [Sizes] with the
Dark colours, two seeded random [Sizes] files over seeded random colours, the range extremes over the extreme colours) the C++
driver (tests/ingame_pack_parity_driver.cpp --sizes) builds MRCustom_P with sizes and MRLayout_P, and the Python side does what
pipeline._build and pipeline.build_layout do (bake with sizes, layout_entries, the manifest body, write_manifest over the same
stand-in pack files): every asset's result and manifest entry, every staged file of both packs and both manifests (as text)
must be identical. Assets whose recipe the C++ side has not ported yet ("later") are left out on both sides.
Also proves the layout package order: the C++ data order equals pipeline.LAYOUT (Path.glob order) and the folder listing.

Needs the game and the tools (no personal paths in the repository): MR_PAKS, MR_RETOC, MR_DDSTOOLS (see
tests/ingame_pack_parity_tests.py); cl on PATH (run_suites.py --msvc) or MR_MSVC_SETUP. Optional MR_PARITY_WORK.
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


def colour_lines(seed):
    rnd = random.Random(seed)
    skin4 = [n for n in cc.BAKED_SKIN if isinstance(cc.SKIN[n], list)]
    skin1 = [n for n in cc.BAKED_SKIN if not isinstance(cc.SKIN[n], list)]
    lines = ["[Colours]"]
    lines += [f"{k} = " + ", ".join(str(round(rnd.uniform(0, 255), rnd.choice((0, 0, 2)))) for _ in range(3)) for k in cc.COLOURS]
    lines += [f"{k} = {round(rnd.uniform(0, 1), 3)}" for k in cc.NUMBERS]
    lines += ["[Tab]"] + [f"{k} = {rnd.randrange(256)}" for k in cc.TAB]
    lines += ["[Skin]"] + [f"{k} = " + ", ".join(str(round(rnd.uniform(0, 1), 4)) for _ in range(4)) for k in skin4]
    return lines + [f"{k} = {round(rnd.uniform(0, 1), 4)}" for k in skin1]


def size_range(name):
    return cc.SCALE_RANGE if name in cc.TEXT_ROLES else cc.SIZE_RANGE


def scenario_files(work):
    """{name: colour file or None}: today's sizes and colours, two seeded random [Sizes] over random colours, the extremes."""
    out = {"sizes-default": None}
    for seed in (1, 2):
        rnd = random.Random(100 + seed)
        lines = colour_lines(seed) + ["[Sizes]"]
        for k in cc.SIZES:
            low, high = size_range(k)
            lines.append(f"{k} = {round(rnd.uniform(low, high), rnd.choice((0, 1, 2)))}")
        path = work / f"sizes-seed{seed}.ini"
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        out[f"sizes-seed{seed}"] = path
    skin4 = [n for n in cc.BAKED_SKIN if isinstance(cc.SKIN[n], list)]
    skin1 = [n for n in cc.BAKED_SKIN if not isinstance(cc.SKIN[n], list)]
    lines = ["[Colours]"] + [f"{k} = " + ("0, 0, 0" if i % 2 else "255, 255, 255") for i, k in enumerate(cc.COLOURS)]
    lines += ["[Skin]"] + [f"{k} = 1, 1, 1, 0" for k in skin4] + [f"{k} = 0" for k in skin1]
    lines += ["[Sizes]"] + [f"{k} = {size_range(k)[i % 2]}" for i, k in enumerate(cc.SIZES)]
    path = work / "sizes-extremes.ini"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    out["sizes-extremes"] = path
    return out


def tree(folder):
    """{relative path: bytes} of every file under folder."""
    folder = Path(folder)
    return {p.relative_to(folder).as_posix(): p.read_bytes() for p in sorted(folder.rglob("*")) if p.is_file()} if folder.is_dir() else {}


def compare_trees(label, py, cpp):
    a, b = tree(py), tree(cpp)
    check(set(a) == set(b), f"{label}: the same staged files (Python only {sorted(set(a) - set(b))[:5]}, C++ only {sorted(set(b) - set(a))[:5]})")
    for rel in sorted(set(a) & set(b)):
        if a[rel] != b[rel]:
            x, y = a[rel], b[rel]
            first = next((i for i in range(min(len(x), len(y))) if x[i] != y[i]), min(len(x), len(y)))
            check(False, f"{label}: {rel} differs: first difference at byte {first} of {len(x)}/{len(y)}")
        else:
            check(True, "")
    return len(set(a) & set(b))


def write_pack(folder, pack, body):
    folder.mkdir(parents=True, exist_ok=True)
    for ext in ("pak", "utoc", "ucas"):
        (folder / f"{pack}.{ext}").write_bytes(f"{pack}.{ext}".encode())
    pipeline.write_manifest(folder, pack, body)
    return (folder / f"{pack}.json").read_text(encoding="utf-8")


def main():
    env = {k: os.environ.get(k) for k in ("MR_PAKS", "MR_RETOC", "MR_DDSTOOLS")}
    missing = [k for k, v in env.items() if not v]
    if missing:
        check(False, f"set {', '.join(missing)} (see the docstring): the parity test needs the game and the tools")
        finish()
    keep = os.environ.get("MR_PARITY_WORK")
    work = Path(keep) if keep else Path(tempfile.mkdtemp(prefix="mr-layout-parity-"))
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    quiet = lambda line: None  # noqa: E731
    try:
        exe = compile_driver(work)
        legacy = work / "legacy"
        args = SimpleNamespace(paks=Path(env["MR_PAKS"]), retoc=Path(env["MR_RETOC"]), ddstools=Path(env["MR_DDSTOOLS"]))
        found = pipeline.extract_all(args, legacy, colours=True, layout=True)
        export, inject = pipeline.dds_tools(args.ddstools)
        exported = {}

        def stock_dds(name):
            if name not in exported:
                uasset = found[name.lower()][0]
                exported[name] = work / "dds" / (uasset.stem + ".dds")
                export(uasset, exported[name])
            return exported[name]

        listing = [p.name for p in Path(pipeline.MENUS).glob("*.json")]
        check(listing == sorted(listing), f"Path.glob lists the data files in name order (the C++ LoadPackData order): {listing}")
        totals = {"baked": 0, "skipped": 0, "later": 0, "layout": 0, "layout_skipped": 0, "files": 0, "layout_pack": 0}
        for name, ini in scenario_files(work).items():
            out = work / name
            r = subprocess.run([str(exe), str(pipeline.MENUS), str(legacy), str(ini) if ini else "-", str(out), "--sizes"],
                               capture_output=True, text=True, errors="replace")
            if r.returncode:
                check(False, f"{name}: driver failed {r.returncode}: {r.stderr[-400:]}")
                continue
            report = json.loads((out / "report.json").read_text(encoding="utf-8"))
            check(report["layout_order"] == list(pipeline.LAYOUT), f"{name}: layout package order C++ {report['layout_order']} Python {list(pipeline.LAYOUT)}")
            colours = cc.pack_values(ini) if ini else cc.pack_values()
            sizes = cc.load_sizes(ini) if ini else dict(cc.SIZES)
            check(report["sizes"] == pipeline.used_sizes(sizes), f"{name}: used_sizes C++ {report['sizes']} Python {pipeline.used_sizes(sizes)}")
            check(set(report["values"]) == set(colours) and all(report["values"][k] == v for k, v in colours.items()),
                  f"{name}: the same colour values")
            # pipeline._build with sizes, over the assets the C++ side has ported
            stage, scratch = work / f"{name}-py/stage", work / f"{name}-py/work"
            style, textures, skipped = {}, {}, []
            for row in report["assets"]:
                asset, recipe = row["asset"], row["recipe"]
                totals[row["result"]] += 1
                if row["result"] == "later":
                    continue
                try:
                    values, entry = pipeline.bake(asset, recipe, colours, pipeline.ART, legacy, stage, scratch, stock_dds, inject, sizes)
                    py_result, py_reason = "baked", ""
                except Exception as error:
                    gc.collect()
                    for ext in (".uasset", ".uexp", ".ubulk"):
                        (stage / "OblivionRemastered/Content" / asset).with_suffix(ext).unlink(missing_ok=True)
                    py_result = "skipped"
                    py_reason = str(error) if isinstance(error, pipeline.SkipAsset) else f"{type(error).__name__}: {error}"
                    skipped.append({"asset": asset, "recipe": recipe, "reason": py_reason})
                    entry = None
                check(row["result"] == py_result and row["reason"] == py_reason,
                      f"{name}: {asset} ({recipe}) C++ {row['result']} ({row['reason']}), Python {py_result} ({py_reason})")
                if py_result != "baked":
                    continue
                style.update(values)
                textures[asset] = entry
                if entry.get("layout_skipped"):
                    skipped.append({"asset": asset, "recipe": "layout", "reason": entry["layout_skipped"]})
                    totals["layout_skipped"] += 1
                if entry.get("layout"):
                    totals["layout"] += 1
                check(row.get("entry") == entry, f"{name}: {asset} manifest entry differs: C++ {row.get('entry')} Python {entry}")
            pipeline.layout_entries(legacy, stage, sizes, {Path(a).name for a in textures}, textures, skipped, quiet)
            totals["files"] += compare_trees(f"{name} MRCustom_P stage", stage, out / "stage")
            py_manifest = write_pack(work / f"{name}-py/colour-pack", "MRCustom_P",
                                     {"style": style, "textures": textures, "skipped": skipped, "sizes": pipeline.used_sizes(sizes)})
            cpp_manifest = (out / "colour-pack/MRCustom_P.json").read_text(encoding="utf-8")
            check(cpp_manifest == py_manifest, f"{name}: MRCustom_P.json differs:\nC++ {cpp_manifest[:600]}\nPython {py_manifest[:600]}")
            check(json.loads(py_manifest).get("header_layout") == {}, f"{name}: the colour pack has header_layout")
            # pipeline.build_layout
            stage2 = work / f"{name}-py/layout-stage"
            t2, s2 = {}, []
            pipeline.layout_entries(legacy, stage2, sizes, set(), t2, s2, quiet)
            totals["layout_pack"] += compare_trees(f"{name} MRLayout_P stage", stage2, out / "layout-stage")
            py_layout = write_pack(work / f"{name}-py/layout-pack", "MRLayout_P", {"sizes": pipeline.used_sizes(sizes), "textures": t2, "skipped": s2})
            cpp_layout = (out / "layout-pack/MRLayout_P.json").read_text(encoding="utf-8")
            check(cpp_layout == py_layout, f"{name}: MRLayout_P.json differs:\nC++ {cpp_layout[:600]}\nPython {py_layout[:600]}")
            check(len(t2) == len(pipeline.LAYOUT) and not s2, f"{name}: every layout package passes its stock check ({len(t2)} staged, skipped {s2})")
        print(f"asset runs: baked {totals['baked']}, skipped {totals['skipped']}, later phases {totals['later']}; colour packages with sizes "
              f"{totals['layout']} (refused {totals['layout_skipped']}); MRCustom_P files compared {totals['files']}, MRLayout_P files {totals['layout_pack']}")
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)
    finish()


main()
