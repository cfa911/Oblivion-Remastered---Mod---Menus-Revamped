"""Run chosen test suites (C++ and Python) and print one table; optionally prove them red on an older revision first.

Usage:
    python tests/run_suites.py [--msvc <setup .bat>] [--baseline <git rev>] <suite> [<suite> ...]
    python tests/run_suites.py --group skin

A suite is a file stem in tests/ (menu_skin_style_tests, dark_skin_wiring_tests, ...). C++ suites are compiled with cl;
the MSVC environment comes from --msvc, else the MR_MSVC_SETUP environment variable, else vcvars64.bat found by vswhere.

--baseline REV checks the tests-first rule mechanically: a temporary git worktree at REV gets the CURRENT tests/ folder,
the same suites run there, and the table shows both columns (old code must fail, new code must pass). A compile failure
on the baseline is reported as such: it is not a behavioural red test.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
GROUPS = {
    "skin": ["menu_skin_style_tests", "inventory_details_skin_tests", "inventory_header_layout_tests", "brush_handle_tests",
             "data_table_rows_tests", "filter_toggle_paint_tests", "dark_skin_wiring_tests", "dark_pack_tests", "dark_pack_switch_tests", "enchanted_paint_tests",
             "ui_style_tests", "config_reader_tests", "inventory_header_transition_tests",
             "container_header_layout_tests"],
    "custom-pack": ["container_title_bake_tests", "custom_pack_data_tests", "custom_colours_tests", "custom_pack_speed_tests", "dark_pack_tests",
                    "dark_pack_switch_tests", "dark_skin_wiring_tests", "custom_colours_reader_tests",
                    "custom_pack_switch_tests", "custom_pack_manifest_tests", "custom_pack_wiring_tests",
                    "pack_builder_colours_tests", "pack_builder_paths_tests", "pack_builder_build_tests",
                    "pack_builder_cli_tests", "custom_pack_alchemy_tests", "custom_pack_scrollbar_tests",
                    "custom_pack_enchanting_tests", "custom_pack_coverage_tests",
                    "custom_pack_altar_tests", "pack_builder_roles_tests", "pack_builder_layout_tests", "layout_pack_switch_tests", "layout_pack_wiring_tests", "custom_pack_rows_tests", "custom_pack_tabs_tests",
                    "custom_pack_details_tests", "container_theming_tests", "container_theming_r2_tests", "paper_invert_tests", "custom_pack_charcoal_tabs_tests"],
}
COUNT = re.compile(r"(\d+) checks, (\d+) failures")


def msvc_setup(explicit):
    if explicit:
        return explicit
    if os.environ.get("MR_MSVC_SETUP"):
        return os.environ["MR_MSVC_SETUP"]
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if vswhere.is_file():
        found = subprocess.run([str(vswhere), "-latest", "-products", "*", "-property", "installationPath"],
                               capture_output=True, text=True).stdout.strip()
        if found:
            return str(Path(found) / "VC/Auxiliary/Build/vcvars64.bat")
    return None


def run_suite(repo, name, work, setup):
    """Returns (status, checks, failures, last line). status: pass, FAIL, compile-fail, missing."""
    tests = repo / "tests"
    work.mkdir(parents=True, exist_ok=True)
    cpp, py = tests / f"{name}.cpp", tests / f"{name}.py"
    if cpp.is_file():
        if not setup:
            return "no-msvc", None, None, "no MSVC environment (pass --msvc or set MR_MSVC_SETUP)"
        exe = work / f"{name}.exe"
        compile_cmd = (f'call "{setup}" >nul 2>nul && cl /nologo /std:c++latest /EHsc /utf-8 "/I{tests}" "/I{repo / "src"}" '
                       f'"{cpp}" "/Fe{exe}" "/Fo{work}\\\\"')
        c = subprocess.run(f'cmd /d /s /c "{compile_cmd}"', cwd=work, capture_output=True, text=True, errors="replace")
        if c.returncode or not exe.is_file():
            errors = [l for l in (c.stdout + c.stderr).splitlines() if " error " in l]
            return "compile-fail", None, None, (errors[:1] or ["compile failed"])[0][-120:]
        r = subprocess.run([str(exe)], cwd=work, capture_output=True, text=True, errors="replace")
    elif py.is_file():
        args = [sys.executable, str(py)]
        if "sys.argv" in py.read_text(encoding="utf-8", errors="replace"):
            args.append(str(work))
        env = dict(os.environ)
        if setup:   # some Python suites compile extracted C++ with cl
            inner = f'call "{setup}" >nul 2>nul && "{sys.executable}" ' + " ".join(f'"{a}"' for a in args[1:])
            r = subprocess.run(f'cmd /d /s /c "{inner}"', cwd=repo, capture_output=True, text=True,
                               errors="replace", env=env)
        else:
            r = subprocess.run(args, cwd=repo, capture_output=True, text=True, errors="replace", env=env)
    else:
        return "missing", None, None, "no such suite"
    out = (r.stdout + r.stderr).strip().splitlines()
    match = next((COUNT.search(l) for l in reversed(out) if COUNT.search(l)), None)
    checks, failures = (int(match[1]), int(match[2])) if match else (None, None)
    return ("pass" if r.returncode == 0 else "FAIL"), checks, failures, (out[-1] if out else "")[-120:]


def cell(result):
    status, checks, failures, _ = result
    return f"{status} {checks}/{failures}" if checks is not None else status


def main():
    cli = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    cli.add_argument("suites", nargs="*")
    cli.add_argument("--group", choices=sorted(GROUPS))
    cli.add_argument("--msvc")
    cli.add_argument("--baseline", help="git revision the current tests must fail on")
    cli.add_argument("--work", type=Path, default=Path(tempfile.gettempdir()) / "mr-suites")
    a = cli.parse_args()
    suites = a.suites + (GROUPS[a.group] if a.group else [])
    if not suites:
        cli.error("name suites or a --group")
    setup = msvc_setup(a.msvc)

    baseline = {}
    if a.baseline:
        tree = a.work / "baseline"
        if tree.exists():
            subprocess.run(["git", "-C", str(REPO), "worktree", "remove", "--force", str(tree)], capture_output=True)
            shutil.rmtree(tree, ignore_errors=True)
        subprocess.run(["git", "-C", str(REPO), "worktree", "add", "--detach", str(tree), a.baseline], check=True,
                       capture_output=True)
        try:
            shutil.rmtree(tree / "tests")
            shutil.copytree(REPO / "tests", tree / "tests")
            for name in suites:
                baseline[name] = run_suite(tree, name, a.work / "base" / name, setup)
        finally:
            subprocess.run(["git", "-C", str(REPO), "worktree", "remove", "--force", str(tree)], capture_output=True)

    rows, bad = [], 0
    for name in suites:
        now = run_suite(REPO, name, a.work / "now" / name, setup)
        bad += now[0] != "pass"
        rows.append((name, baseline.get(name), now))
    head = ["Suite", f"Baseline {a.baseline} (checks/failures)", "Now (checks/failures)", "Note"] if a.baseline else \
           ["Suite", "Result (checks/failures)", "Note"]
    print(" | ".join(head))
    print(" | ".join("---" for _ in head))
    for name, base, now in rows:
        note = now[3] if now[0] != "pass" else ""
        if base is not None:
            if base[0] == "pass":
                note = (note + " ; ").lstrip(" ;") + "NOT red on baseline"
            print(f"{name} | {cell(base)} | {cell(now)} | {note}")
        else:
            print(f"{name} | {cell(now)} | {note}")
    print(f"suites {len(rows)}, failing now {bad}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
