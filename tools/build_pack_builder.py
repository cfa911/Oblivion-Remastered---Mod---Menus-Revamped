"""Freeze the Custom pack builder: build/pack_builder/dist/PackBuilder (PyInstaller onedir, no UPX), ready to copy into
Plugins/MenusRevamped/PackBuilder. The output is git-ignored and never committed.

Usage:
  python tools/build_pack_builder.py --python <python with PyInstaller and Pillow> --ddstools <UE4-DDS-Tools folder>
                                     --retoc <folder of retoc.exe and its LICENSE> [--out build/pack_builder]

Layout of PackBuilder/:
  MenusRevampedPackBuilder.exe + _internal/   the window (no argument) or the make_dark_pack.py command line (any argument)
  menus/<menu>.json                           the data files (a mod update can add assets without a new exe)
  retoc/retoc.exe + LICENSE                   retoc downloads Oodle (oo2core_9_win64.dll) next to itself on its first run;
                                              Oodle is proprietary and never shipped (owner decision, Phase 0)
  licences/                                   the notices of everything bundled (KB plan "Phase 0 results" section 6)
  README.txt
Not shipped: owner art (png recipe, development only; no data file uses it since Phase 6), CustomColours.ini (the plugin writes it), Oodle.
Pillow's _avif/_webp and the standard library's _ssl/_hashlib (OpenSSL) are excluded: the pipeline uses none of them
(hashlib falls back to its built-in SHA-256); the frozen --check proves the build is unchanged.
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAME = "MenusRevampedPackBuilder"
EXCLUDES = ("PIL._avif", "PIL._webp", "_ssl", "_hashlib", "ssl")
README = """Menus Revamped - Custom Pack Builder
====================================

Run MenusRevampedPackBuilder.exe from this folder (Plugins/MenusRevamped/PackBuilder). It finds the game by itself.

- Pick colours by menu and visible element. Each entry corresponds to a key in ../CustomColours.ini.
- Build pack: applies the marked baked colours, including both Inventory laurel controls. A Phase 4 pack still uses the
  runtime laurel tint until you rebuild. A new pack uses the tint from its last build.
- Restart game: choose Save, then restart to apply these runtime colours. The inactive keys (sort picker, old tab greys,
  details band and action button tints) are kept for existing INI files.
- Build: writes ../CustomPack/MRCustom_P.* (the previous pack is kept as .bak). The plugin installs the pack at the next game
  start when [Style] uPreset=2 in MenusRevamped.ini. An asset that fails its check is skipped and listed.
- Reset to Dark: the default colours. Open log: the last build's log (PackBuilder.log).

The first build needs internet: retoc downloads Oodle (oo2core_9_win64.dll, Epic Games' decompressor, not included) into
the retoc folder to read the game's files.

Command line: MenusRevampedPackBuilder.exe --help (the same options as tools/make_dark_pack.py in the source).
Bundled software and their licences: see the licences folder.
"""


def run(cmd, **kw):
    print(">", " ".join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def ask(python, code):
    return subprocess.run([str(python), "-c", code], check=True, capture_output=True, text=True).stdout.strip()


def main():
    cli = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    cli.add_argument("--python", type=Path, default=Path(sys.executable))
    cli.add_argument("--ddstools", type=Path, required=True, help="the UE4-DDS-Tools folder (with src/ and LICENSE)")
    cli.add_argument("--retoc", type=Path, required=True, help="the folder of retoc.exe and its LICENSE")
    cli.add_argument("--out", type=Path, default=ROOT / "build/pack_builder")
    a = cli.parse_args()
    src = a.ddstools / "src"
    for need in (src / "unreal/uasset.py", a.ddstools / "LICENSE", a.retoc / "retoc.exe", a.retoc / "LICENSE"):
        if not need.is_file():
            sys.exit(f"missing {need}")
    work, dist = a.out / "work", a.out / "dist"
    shutil.rmtree(dist, ignore_errors=True)
    cmd = [a.python, "-m", "PyInstaller", "--noconfirm", "--clean", "--onedir", "--windowed", "--noupx", "--name", NAME,
           "--distpath", dist, "--workpath", work, "--specpath", work, "--paths", ROOT / "tools", "--paths", src,
           "--collect-submodules", "unreal", "--collect-submodules", "directx", "--hidden-import", "make_dark_pack"]
    for module in EXCLUDES:
        cmd += ["--exclude-module", module]
    run(cmd + [ROOT / "tools/pack_builder_exe.py"])
    folder = dist / "PackBuilder"
    shutil.rmtree(folder, ignore_errors=True)
    (dist / NAME).rename(folder)

    (folder / "menus").mkdir()
    for data in sorted((ROOT / "tools/menus").glob("*.json")):
        shutil.copy2(data, folder / "menus" / data.name)
    (folder / "retoc").mkdir()
    for name in ("retoc.exe", "LICENSE"):
        shutil.copy2(a.retoc / name, folder / "retoc" / name)

    licences = folder / "licences"
    licences.mkdir()
    base = Path(ask(a.python, "import sys; print(sys.base_prefix)"))
    pillow = Path(ask(a.python, "import importlib.metadata as m; d = m.distribution('pillow'); "
                                "print(next(d.locate_file(f) for f in d.files if str(f).endswith('licenses/LICENSE')))"))
    pyinstaller = Path(ask(a.python, "import importlib.metadata as m; d = m.distribution('pyinstaller'); "
                                     "print(next(d.locate_file(f) for f in d.files if str(f).endswith('COPYING.txt')))"))
    tcl = next((base / "tcl").rglob("license.terms"), None)
    for source, name in ((a.retoc / "LICENSE", "retoc-LICENSE.txt"), (a.ddstools / "LICENSE", "UE4-DDS-Tools-LICENSE.txt"),
                         (pillow, "Pillow-LICENSE.txt"), (base / "LICENSE.txt", "Python-LICENSE.txt"),
                         (tcl, "Tcl-Tk-license.terms.txt"), (pyinstaller, "PyInstaller-COPYING.txt")):
        if source is None or not Path(source).is_file():
            sys.exit(f"licence file missing: {name} ({source})")
        shutil.copy2(source, licences / name)
    (folder / "README.txt").write_text(README, encoding="utf-8")

    # what must never ship
    oodle = [p for p in folder.rglob("oo2core*")]
    if oodle:
        sys.exit(f"Oodle must not ship: {oodle}")
    # paths of this PC (the profile, the checkout, the tool folders) must not be baked into anything shipped; a bare user
    # name is not searched: third-party files hold common first names (copyright lines, time zone and Unicode names)
    private = [v for v in (os.environ.get("USERPROFILE"), str(ROOT), str(a.ddstools), str(a.retoc), str(base)) if v and len(v) > 3]
    found = []
    for path in folder.rglob("*"):
        if path.is_file():
            data = path.read_bytes().lower()
            for value in private:
                for variant in {value, value.replace("\\", "/")}:
                    if any(e.lower() in data for e in (variant.encode(), variant.encode("utf-16-le"))):
                        found.append(f"{path.relative_to(folder).as_posix()} ({variant})")
    if found:
        print("WARNING: a path of this PC appears in:", ", ".join(sorted(set(found))))
    files = [p for p in folder.rglob("*") if p.is_file()]
    size = sum(p.stat().st_size for p in files)
    print(f"{folder}: {len(files)} files, {size / 1e6:.1f} MB (retoc {(folder / 'retoc/retoc.exe').stat().st_size / 1e6:.1f} MB)")
    sys.exit(1 if found else 0)


if __name__ == "__main__":
    main()
