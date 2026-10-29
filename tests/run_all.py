"""Run every test (Python and C++) and print one row per suite.

Usage (inside an MSVC x64 developer prompt):
    python tests/run_all.py <output-directory> [<Improved Wheels Plus checkout>]
Without the wheel checkout, integration_boundaries_tests.py is skipped.
"""
import subprocess
import sys
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]).resolve()
wheel = sys.argv[2] if len(sys.argv) > 2 else None
out.mkdir(parents=True, exist_ok=True)
tests = repo / "tests"
rows = []

for py in sorted(tests.glob("*.py")):
    if py.name in ("source_paths.py", "run_all.py"):
        continue
    work = out / py.stem
    work.mkdir(exist_ok=True)
    args = [sys.executable, str(py)]
    if py.name == "integration_boundaries_tests.py":   # reads the wheel's lookup bodies (read-only)
        if not wheel:
            rows.append((py.name, 0, ["skipped: no wheel checkout given"]))
            continue
        args += [str(repo), wheel, str(work)]
    elif "sys.argv" in py.read_text(encoding="utf-8"):
        args.append(str(work))
    r = subprocess.run(args, cwd=repo, capture_output=True, text=True, errors="replace")
    (work / "log.txt").write_text(r.stdout + r.stderr, encoding="utf-8")
    rows.append((py.name, r.returncode, (r.stdout + r.stderr).strip().splitlines()[-1:] or [""]))

for cpp in sorted(tests.glob("*.cpp")):
    work = out / cpp.stem
    work.mkdir(exist_ok=True)
    exe = work / (cpp.stem + ".exe")
    c = subprocess.run(["cl", "/nologo", "/std:c++latest", "/EHsc", "/utf-8", f"/I{tests}", f"/I{repo / 'src'}",
                        str(cpp), f"/Fe{exe}", f"/Fo{work}\\"], cwd=work, capture_output=True, text=True, errors="replace")
    if c.returncode:
        (work / "log.txt").write_text(c.stdout + c.stderr, encoding="utf-8")
        rows.append((cpp.name, "compile-fail", c.stdout.strip().splitlines()[-3:]))
        continue
    r = subprocess.run([str(exe)], cwd=work, capture_output=True, text=True, errors="replace")
    (work / "log.txt").write_text(r.stdout + r.stderr, encoding="utf-8")
    rows.append((cpp.name, r.returncode, (r.stdout + r.stderr).strip().splitlines()[-1:] or [""]))

bad = 0
for name, code, tail in rows:
    if code != 0:
        bad += 1
    print(f"{'PASS' if code == 0 else 'FAIL'} | {name} | exit {code} | {' / '.join(tail)[:140]}")
print(f"suites {len(rows)}, failing {bad}")
sys.exit(1 if bad else 0)
