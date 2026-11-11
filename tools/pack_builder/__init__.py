"""Menus Revamped Custom pack builder (KB mods/menus-revamped/evidence/custom-pack-builder-plan.md, Phase 4).

pipeline.py   the recipes and the build (stock assets -> re-coloured pack + manifest)
app.py        build_pack(): the one build of the CLI (tools/make_dark_pack.py) and of the window
install.py    the replace of the installed pack (all four files together, the previous set kept as .bak)
paths.py      where the game, retoc, the data files and the colour file are (from the exe's own folder when frozen)
model.py      the window's colour model (CustomColours.ini entries, ranges, swatches), no Tk
window.py     the Tk window (thin: it calls model.py and app.py)
"""

VERSION = "0.4.0"   # written into the manifest as builder_version


class BuildError(SystemExit):
    """A build that cannot go on: its text is the message for the user. A SystemExit, so the CLI stops with it."""

    def __str__(self):
        return str(self.code)
