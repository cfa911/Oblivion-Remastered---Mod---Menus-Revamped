"""In-game Pack Builder Phase 2: the C++ builder model (src/builder/ColourModel.h) saves the same CustomColours.ini as the
Python builder (tools/pack_builder/model.py) for the same edits, byte for byte, and refuses the same edits.

The same edit scripts run on both: hand-written scenarios (every role, own values, [Skin] decimals, sizes, Reset to Dark, an old
file without [Roles]/[Sizes], reloading a file the other side wrote) and a seeded random sequence. The C++ side is
tests/ingame_builder_parity_driver.cpp, compiled with cl (cl on PATH, as run_suites.py --msvc provides, or MR_MSVC_SETUP).
"""
import os
import random
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import source_paths

root = source_paths.ROOT
sys.path.insert(0, str(root / "tools"))
import custom_colours as cc  # noqa: E402
from pack_builder import model  # noqa: E402

fails = []
count = 0


def check(ok, why):
    global count
    count += 1
    if not ok:
        fails.append(why)


def compile_driver(work):
    exe = work / "driver.exe"
    src = root / "tests/ingame_builder_parity_driver.cpp"
    cmd = f'cl /nologo /std:c++latest /EHsc /utf-8 /I"{root / "src"}" "{src}" /Fe"{exe}" /Fo"{work}\\\\"'
    if not shutil.which("cl"):
        setup = os.environ.get("MR_MSVC_SETUP")
        if not setup:
            print("no cl on PATH and no MR_MSVC_SETUP (pass --msvc to run_suites.py)")
            print("1 checks, 1 failures")
            sys.exit(1)
        cmd = f'call "{setup}" >nul 2>nul && ' + cmd
    bat = work / "build.bat"
    bat.write_text("@echo off\n" + cmd + "\n", encoding="utf-8")
    r = subprocess.run(["cmd", "/d", "/c", str(bat)], capture_output=True, text=True, errors="replace")
    if r.returncode or not exe.is_file():
        print((r.stdout + r.stderr)[-3000:])
        print("1 checks, 1 failures")
        sys.exit(1)
    return exe


def key_of(section, name):
    return name if section == "Colours" else f"{section}.{name}"


def split_key(key):
    if key.startswith("Skin.") or key.startswith("Tab."):
        s, n = key.split(".", 1)
        return s, n
    return "Colours", key


def run_python(lines):
    """The same script on the Python model; returns the refused line numbers."""
    m = model.ColourModel(Path(tempfile.gettempdir()) / "mr-parity-none.ini")
    refused = []
    for number, line in enumerate(lines, 1):
        f = line.split("\t")
        try:
            if f[0] == "load":
                m = model.ColourModel(f[1])
            elif f[0] == "role":
                m.set_role(f[1], [float(c) for c in f[2].split(",")])
            elif f[0] == "text":
                s, n = split_key(f[1])
                m.set_text(s, n, f[2])
            elif f[0] == "rgb":
                s, n = split_key(f[1])
                m.set_rgb(s, n, [float(c) for c in f[2].split(",")])
            elif f[0] == "size":
                m.set_size(f[1], f[2])
            elif f[0] == "reset":
                m.reset_element(f[1])
            elif f[0] == "resetsize":
                m.reset_size(f[1])
            elif f[0] == "dark":
                m.reset_to_dark()
            elif f[0] == "save":
                m.save(f[1])
        except (ValueError, model.ModelError):
            refused.append(number)
    return refused


def run_cpp(exe, lines, work):
    script = work / "script.txt"
    script.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    r = subprocess.run([str(exe), str(script)], capture_output=True, text=True, errors="replace")
    return [int(l.split()[1]) for l in r.stdout.splitlines() if l.startswith("ERR ")]


def compare(exe, name, lines, work):
    """Runs the script on both sides; every 'save' path gets a .py and a .cpp twin that must be identical."""
    py_lines = [l.replace("{side}", "py") for l in lines]
    cpp_lines = [l.replace("{side}", "cpp") for l in lines]
    py_refused = run_python(py_lines)
    cpp_refused = run_cpp(exe, cpp_lines, work)
    check(py_refused == cpp_refused, f"{name}: the same edits are refused (Python {py_refused}, C++ {cpp_refused})")
    for py_line, cpp_line in zip(py_lines, cpp_lines):
        if py_line.startswith("save\t"):
            a, b = Path(py_line.split("\t")[1]), Path(cpp_line.split("\t")[1])
            if not (a.is_file() and b.is_file()):
                check(False, f"{name}: both sides saved {a.name}")
                continue
            ta, tb = a.read_bytes(), b.read_bytes()
            if ta != tb:
                la, lb = ta.decode("utf-8", "replace").splitlines(), tb.decode("utf-8", "replace").splitlines()
                diff = next((f"line {i + 1}: py {x!r} / cpp {y!r}" for i, (x, y) in enumerate(zip(la, lb)) if x != y),
                            f"lengths {len(la)} / {len(lb)}")
                check(False, f"{name}: {a.name} differs ({diff})")
            else:
                check(True, "")


ALL_KEYS = ([key_of("Colours", n) for n in list(cc.COLOURS) + list(cc.NUMBERS)] + [key_of("Tab", n) for n in cc.TAB]
            + [key_of("Skin", n) for n in cc.SKIN])
ROLE_MEMBERS = [m for r in cc.ROLES.values() for m in r["members"]]
FONTS = [f"font.{m}" for r in cc.TEXT_ROLES.values() for m in r["members"]]


def value_text(rng, key):
    section, name = split_key(key)
    digits = rng.choice([0, 0, 1, 2, 4, 6, 9])
    if section == "Skin":
        n = 4 if isinstance(cc.SKIN[name], list) else 1
        return ", ".join(f"{rng.random():.{digits}f}" for _ in range(n))
    if name in cc.COLOURS:
        return ", ".join(f"{rng.uniform(0, 255):.{digits}f}" for _ in range(3))
    top = 1.0 if name in cc.NUMBERS else 255.0
    return f"{rng.uniform(0, top):.{digits}f}"


def random_script(rng, out, steps):
    lines = []
    for i in range(steps):
        pick = rng.random()
        if pick < 0.25:
            role = rng.choice(list(cc.ROLES))
            lines.append(f"role\t{role}\t" + ",".join(str(rng.randint(-5, 260)) for _ in range(3)))
        elif pick < 0.45:
            key = rng.choice(ALL_KEYS)
            lines.append(f"text\t{key}\t{value_text(rng, key)}" if rng.random() > 0.05 else f"text\t{key}\t1, x")
        elif pick < 0.60:
            key = rng.choice(ALL_KEYS)
            lines.append(f"rgb\t{key}\t" + ",".join(str(rng.randint(0, 255)) for _ in range(3)))
        elif pick < 0.70:
            lines.append(f"reset\t{rng.choice(ROLE_MEMBERS)}")
        elif pick < 0.85:
            key = rng.choice(list(cc.SIZES))
            low, high = cc.SCALE_RANGE if key in cc.TEXT_ROLES else cc.SIZE_RANGE
            v = rng.choice([rng.randint(low - 2, high + 2), round(rng.uniform(low, high), rng.choice([1, 2, 5]))])
            lines.append(f"size\t{key}\t{v}")
        elif pick < 0.93:
            lines.append(f"resetsize\t{rng.choice(FONTS)}")
        elif pick < 0.95:
            lines.append("dark")
        else:
            lines.append(f"save\t{out}/r{i}.{{side}}.ini")
    lines.append(f"save\t{out}/r-end.{{side}}.ini")
    return lines


def main():
    work = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(tempfile.mkdtemp())
    work.mkdir(parents=True, exist_ok=True)
    out = work / "out"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir()
    exe = compile_driver(work)

    compare(exe, "defaults", [f"save\t{out}/default.{{side}}.ini"], work)

    every_role = [f"role\t{r}\t{(i * 37) % 256},{(i * 71 + 13) % 256},{(i * 113 + 200) % 256}" for i, r in enumerate(cc.ROLES)]
    compare(exe, "every role", every_role + [f"save\t{out}/roles.{{side}}.ini"], work)

    compare(exe, "own values, decimals, sizes", [
        "role\tbodyText\t200,180,160", "text\tnameBox\t1, 2, 3", "rgb\tSkin.rule\t12,34,56",
        "text\tSkin.guide\t0.123456789, 0.5, 0.00005, 1", "text\tdetailsPage\t0.0003", "text\tTab.gap\t12.5",
        "rgb\tdetailsPage\t40,50,60", "rgb\tTab.outline\t200,10,10", "rgb\tSkin.scrollTrack\t1,2,3",
        "size\ttextValueScale\t137.5", "size\tfont.InventoryMainPart.inv_money\t15.25", "size\ttextValueScale\t90",
        "size\tinventoryRowHeight\t0", "text\trowText\t1, 2", "role\tlines\t300,0,0",
        f"save\t{out}/own.{{side}}.ini", "reset\tnameBox", "resetsize\tfont.InventoryMainPart.inv_money",
        f"save\t{out}/own-reset.{{side}}.ini", "dark", f"save\t{out}/own-dark.{{side}}.ini"], work)

    old = out / "old.ini"
    old.write_text("; an old file\n[Colours]\nrowText = 10, 20, 30\naltarPaper = 24, 25, 28\npanel = 41.5, 41, 41\n"
                   "detailsPage = 0.02\n[Tab]\ngap = 30\n[Skin]\nring = 0.5, 0.5, 0.5, 1\nrule = 0.2, 0.2, 0.2, 0.5\n"
                   "[Roles]\nring = 1, 2, 3\n", encoding="utf-8")
    compare(exe, "old file", [f"load\t{old}", f"save\t{out}/old.{{side}}.ini", "role\tbodyText\t100,100,100",
                              "role\tlistPanel\t60,60,60", f"save\t{out}/old2.{{side}}.ini"], work)

    # a file one side wrote, read by the other
    compare(exe, "reload", [f"load\t{out}/own.py.ini", "role\tmetal\t90,80,70", f"save\t{out}/reload-a.{{side}}.ini",
                            f"load\t{out}/own.cpp.ini", "size\ttextBodyScale\t55", f"save\t{out}/reload-b.{{side}}.ini"], work)

    compare(exe, "broken file", [f"load\t{out}/broken.ini", f"save\t{out}/broken.{{side}}.ini"]
            if (out / "broken.ini").write_text("[Colours]\npanel = 1, 2\n", encoding="utf-8") else [], work)

    rng = random.Random(20261121)
    for n in range(6):
        compare(exe, f"random {n}", random_script(rng, out, 250), work)

    for why in fails:
        print("FAIL", why)
    print(f"{count} checks, {len(fails)} failures")
    sys.exit(1 if fails else 0)


main()
