"""The replace of the installed pack: <pack>.pak/.utoc/.ucas/.json together, the previous set kept as .bak.

The new set must be complete before anything moves; a replace failing half-way (a file in use) puts the previous set back, so the
folder never holds a half-written pack. The builder writes only CustomPack/: the plugin copies the pack into ~mods at the next
game start (src/core/rules/DarkPackSwitch.h).
"""
import os
from contextlib import suppress
from pathlib import Path

from pack_builder import BuildError

EXTS = ("pak", "utoc", "ucas", "json")


def pack_files(pack):
    return [f"{pack}.{ext}" for ext in EXTS]


def install(built, target, pack, replace=os.replace):
    """Moves the four files of built/ into target/; returns their new paths. replace: os.replace (a parameter for the tests)."""
    built, target = Path(built), Path(target)
    names = pack_files(pack)
    missing = [n for n in names if not (built / n).is_file()]
    if missing:
        raise BuildError(f"the new pack is incomplete (missing {', '.join(missing)}): the installed pack was not changed")
    target.mkdir(parents=True, exist_ok=True)
    for name in names:   # the backup of the build before last goes first
        (target / (name + ".bak")).unlink(missing_ok=True)
    backed, placed = [], []
    try:
        for name in names:
            if (target / name).exists():
                replace(target / name, target / (name + ".bak"))
                backed.append(name)
        for name in names:
            replace(built / name, target / name)
            placed.append(name)
    except OSError as error:
        for name in placed:
            with suppress(OSError):
                (target / name).unlink()
        for name in backed:
            with suppress(OSError):
                os.replace(target / (name + ".bak"), target / name)
        raise BuildError(f"could not replace the pack in {target} ({error}); the previous pack was put back. "
                         f"Close the game and build again.") from None
    return [target / n for n in names]
