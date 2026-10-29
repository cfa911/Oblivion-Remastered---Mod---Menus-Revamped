"""Where each source unit the tests read lives. Moving or splitting a file changes this table only.

A unit can span several files (a split file): read() joins them in order, so a marker search still finds
each function once.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

UNITS = {
    "picker": ["sortingoverhaul/GearFilter.cpp"],
    "container": ["sortingoverhaul/Container.cpp"],
    "gear-data": ["sortingoverhaul/GearData.cpp"],
    "hover": ["revamped/HoverFix.cpp"],
    "search": ["menusearch/MenuSearch.cpp"],
    "keyboard-api": ["searchkb/SearchApi.cpp"],
    "keyboard-input": ["searchkb/XInputHook.cpp"],
    "input-driver": ["sortingoverhaul/Driver.cpp"],
    "stolen-filter": ["sortingoverhaul/StolenFilter.h"],
}

# Include folders for compiling extracted bodies (headers such as TransferHold.h, ListLifecycle.h).
INCLUDE_DIRS = ["sortingoverhaul", "revamped"]

SORTING_ASSETS = "sortingoverhaul/assets/SortingOverhaul"
GONE_PROBE = "revamped/StolenProbe.cpp"


def paths(unit):
    return [ROOT / p for p in UNITS[unit]]


def read(unit, encoding="utf-8-sig"):
    return "\n".join(p.read_text(encoding=encoding) for p in paths(unit))


def include_flags():
    return [f"/I{ROOT / d}" for d in INCLUDE_DIRS]
