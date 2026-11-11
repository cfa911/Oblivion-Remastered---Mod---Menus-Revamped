"""Custom pack builder Phase 4 (owner 2026-11-11): the builder finds the game by itself.
- from its own folder (Plugins/MenusRevamped/PackBuilder) up to OblivionRemastered/Content/Paks;
- a folder picked by hand may be the game folder, OblivionRemastered or Paks; a folder without
  OblivionRemastered-Windows.utoc gives a clear message;
- the frozen exe's defaults sit next to it (retoc/, menus/, art/, the log) and in the plugin folder (CustomColours.ini,
  CustomPack/); from source they are the repo's own folders;
- Oodle: retoc downloads oo2core_9_win64.dll next to itself, so a missing DLL in a folder that cannot be written is named
  before the build starts."""
import importlib
import sys
import tempfile
from pathlib import Path

import source_paths

fails = []
count = 0


def check(ok, why):
    global count
    count += 1
    if not ok:
        fails.append(why)


root = source_paths.ROOT
sys.path.insert(0, str(root / "tools"))
try:
    paths = importlib.import_module("pack_builder.paths")
except Exception as error:
    paths = None
    check(False, f"tools/pack_builder/paths.py imports ({type(error).__name__}: {error})")


def fn(name):
    f = getattr(paths, name, None)
    check(callable(f), f"pack_builder.paths has {name}()")
    return f


find_paks, check_paks, defaults, oodle_problem = (fn(n) for n in ("find_paks", "check_paks", "defaults", "oodle_problem"))


def message_of(call):
    """(returned value, message of the exception or None); a crash is reported as such."""
    try:
        return call(), None
    except SystemExit as stop:
        return None, str(stop.code)
    except Exception as error:
        return None, f"CRASH {type(error).__name__}: {error}"


with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    game = temp / "Oblivion Remastered"
    paks = game / "OblivionRemastered/Content/Paks"
    paks.mkdir(parents=True)
    (paks / "OblivionRemastered-Windows.utoc").write_bytes(b"")
    plugin = game / "OblivionRemastered/Binaries/Win64/OBSE/Plugins/MenusRevamped"
    builder = plugin / "PackBuilder"
    builder.mkdir(parents=True)
    elsewhere = temp / "Downloads/PackBuilder"
    elsewhere.mkdir(parents=True)

    if find_paks:
        check(find_paks(builder) == paks, f"found from the exe's folder ({find_paks(builder)})")
        check(find_paks(elsewhere) is None, "a builder outside the game finds nothing (no guess)")
    if check_paks:
        for picked in (game, game / "OblivionRemastered", paks):
            got, error = message_of(lambda: check_paks(picked))
            check(got == paks, f"a picked {picked.name!r} folder gives the Paks folder ({got}, {error})")
        got, error = message_of(lambda: check_paks(elsewhere))
        check(got is None and error and not error.startswith("CRASH") and "OblivionRemastered-Windows.utoc" in error
              and str(elsewhere) in error, f"a folder without the game's container gives a clear message ({error!r:.200})")

    if defaults:
        saved = {k: getattr(sys, k, None) for k in ("frozen", "executable")}
        try:
            sys.frozen = True
            sys.executable = str(builder / "MenusRevampedPackBuilder.exe")
            d = defaults()
        finally:
            for k, v in saved.items():
                if v is None:
                    if hasattr(sys, k):
                        delattr(sys, k)
                else:
                    setattr(sys, k, v)
        check(d.get("retoc") == builder / "retoc/retoc.exe", f"frozen: retoc ships in PackBuilder/retoc ({d.get('retoc')})")
        check(d.get("menus") == builder / "menus", "frozen: the menu data files ship in PackBuilder/menus")
        check(d.get("art") == builder / "art", "frozen: owner art (if any) in PackBuilder/art")
        check(d.get("log") == builder / "PackBuilder.log", "frozen: the log is PackBuilder/PackBuilder.log")
        check(d.get("colours") == plugin / "CustomColours.ini", "frozen: the colour file is the plugin's CustomColours.ini")
        check(d.get("out") == plugin / "CustomPack", "frozen: the pack goes to the plugin's CustomPack folder")
        check(d.get("paks") == paks, f"frozen: the game's Paks found from the exe ({d.get('paks')})")
        d = defaults()
        check(d.get("menus") == root / "tools/menus" and d.get("art") == root / "assets-src/DarkPack",
              "from source: the repo's tools/menus and assets-src/DarkPack")
        check(d.get("out") == root / "assets/CustomPack" and d.get("colours") == root / "config/CustomColours.ini",
              "from source: assets/CustomPack and config/CustomColours.ini")

    if oodle_problem:
        retoc_dir = temp / "retoc"
        retoc_dir.mkdir()
        retoc = retoc_dir / "retoc.exe"
        text = oodle_problem(retoc)
        check(bool(text) and "retoc" in text and str(retoc) in text, f"a missing retoc is named ({text!r:.120})")
        retoc.write_bytes(b"")
        check(oodle_problem(retoc) is None, "retoc in a writable folder: no problem (it downloads Oodle on its first run)")
        saved_writable = getattr(paths, "folder_writable", None)
        check(callable(saved_writable), "pack_builder.paths has folder_writable()")
        if callable(saved_writable):
            check(saved_writable(retoc_dir) is True, "a temp folder is writable")
            paths.folder_writable = lambda folder: False
            try:
                text = oodle_problem(retoc)
                check(bool(text) and "oo2core_9_win64.dll" in text and "writ" in text and "internet" in text,
                      f"no DLL and a read-only folder: the message explains the download ({text!r:.200})")
                (retoc_dir / "oo2core_9_win64.dll").write_bytes(b"")
                check(oodle_problem(retoc) is None, "a DLL next to retoc: no problem even read-only")
            finally:
                paths.folder_writable = saved_writable

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
