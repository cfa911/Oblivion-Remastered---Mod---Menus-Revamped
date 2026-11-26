"""In-game Pack Builder Phase 3: the C++ build pipeline (src/builder/PackBuild.h) bakes the same files as the Python builder
(tools/pack_builder/pipeline.py), byte for byte, from the same stock assets of the installed game.

retoc extracts the stock assets once (pipeline.extract_all); then for each colour scenario (the Dark defaults, seeded random
colour files, extremes) the C++ driver (tests/ingame_pack_parity_driver.cpp) and pipeline.bake each bake every asset whose
recipe the C++ side has ported; every staged .uasset/.uexp/.ubulk must be identical, the manifest entries equal, the same
assets skipped, and the colour values equal (custom_colours.pack_values). Phase 3c: every recipe is ported, so no row may be
"later"; recipe kinds no data row uses are baked on chosen stock assets too (EXTRA, compared the same way; png with a
generated owner-art PNG). Phase 4a: the driver also bakes every texture row through the live preview
(src/builder/LivePreviewCore.h) and reports any row whose preview bytes differ from the staged package's mips.

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


# recipe kinds no data row uses: (recipe whose first data asset is borrowed, recipe baked on it)
EXTRA = [("parchment", "parchment:enchanting"), ("parchment", "paper_scale:0.9"), ("parchment", "mask_peak:120"),
         ("popup_panel:", "popup_paper:0.6"), ("popup_panel:", "popup_paper:0"), ("paper_invert:1.5", "smooth_grain:1.5:0.6"),
         ("paper_invert:1.5", "page_grain:3:7.5"), ("details_invert:", "details_smooth:1.5:0.6"), ("details_invert:", "details_page"),
         ("details_invert:", "gray"), ("charcoal:inventory", "tab:1"), ("charcoal:magic", "magic_tab:2"), ("charcoal:inventory", "tab_paper"),
         ("charcoal:magic", "tab_paper"), ("charcoal_paper:enchanting", "tab_strip"), ("gray_smooth:", "png")]


def extra_pairs():
    out = []
    for borrow, recipe in EXTRA:
        asset = next((a for a, r in pipeline.TEXTURES.items() if r == borrow or r.startswith(borrow)), None)
        if asset:
            out.append([asset, recipe])
    return out


def compare(name, rows, stage, out_stage, colours, legacy, stock_dds, inject, scratch, totals):
    for row in rows:
        asset, recipe = row["asset"], row["recipe"]
        totals[row["result"]] += 1
        check(row["result"] != "later", f"{name}: {asset} ({recipe}) is still 'later' in C++")
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
        if py_result == "skipped" and row["result"] == "skipped":
            check(row["reason"] == py_reason, f"{name}: {asset} ({recipe}) skip reason C++ {row['reason']!r} Python {py_reason!r}")
        if py_result != "baked" or row["result"] != "baked":
            continue
        check(row["entry"] == entry, f"{name}: {asset} manifest entry differs: C++ {row['entry']} Python {entry}")
        for ext in (".uasset", ".uexp", ".ubulk"):
            py = (stage / "OblivionRemastered/Content" / asset).with_suffix(ext)
            cpp = (out_stage / "OblivionRemastered/Content" / asset).with_suffix(ext)
            if not py.is_file() and not cpp.is_file():
                continue
            same = py.is_file() and cpp.is_file() and py.read_bytes() == cpp.read_bytes()
            if not same and py.is_file() and cpp.is_file():
                a, b = py.read_bytes(), cpp.read_bytes()
                first = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
                why = f"first difference at byte {first} of {len(a)}/{len(b)}"
                MISMATCH.append((name, asset, recipe))
            else:
                why = "missing on one side"
            check(same, f"{name}: {asset}{ext} ({recipe}) differs: {why}")


MISMATCH = []


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
        extra = extra_pairs()
        check(len(extra) == len(EXTRA), f"extra pairs found {len(extra)} of {len(EXTRA)}")
        (work / "extra.json").write_text(json.dumps(extra), encoding="utf-8")
        art = work / "art"   # the png recipe: one generated owner PNG
        art.mkdir()
        png_asset = next((a for a, r in extra if r == "png"), None)
        if png_asset:
            from PIL import Image
            im = Image.new("RGBA", (37, 23))
            im.putdata([(x * 7 % 256, y * 11 % 256, (x * y) % 256, 255) for y in range(23) for x in range(37)])
            im.save(art / (Path(png_asset).name + ".png"))
        pipeline.ART = art
        for name, ini in scenario_files(work).items():
            out = work / name
            r = subprocess.run([str(exe), str(pipeline.MENUS), str(legacy), str(ini) if ini else "-", str(out),
                                "--extra", str(work / "extra.json"), "--art", str(art)],
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
            compare(name, report["assets"], stage, out / "stage", colours, legacy, stock_dds, inject, scratch, totals)
            for i, row in enumerate(report.get("extra", [])):   # one stage folder per extra row: an asset repeats
                compare(name, [row], work / f"{name}-py/extra-stage/{i}", out / "extra-stage" / str(i), colours, legacy, stock_dds, inject,
                        scratch, totals)
            check(len(report.get("extra", [])) == len(extra), f"{name}: extra rows {len(report.get('extra', []))} of {len(extra)}")
            preview = report.get("preview", {})   # Phase 4a: the live preview's bytes = the staged (Python-identical) mips
            check(preview.get("textures", 0) > 100, f"{name}: live preview baked {preview.get('textures', 0)} texture rows")
            check(preview.get("mismatches") == [], f"{name}: live preview differs from the build for {preview.get('mismatches')}")
        print(f"asset runs (4 scenarios, data + extra): baked {totals['baked']}, skipped {totals['skipped']}, later {totals['later']}")
        if MISMATCH:
            print("byte mismatches:", sorted(set((a, r) for _, a, r in MISMATCH)))
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)
    finish()


main()
