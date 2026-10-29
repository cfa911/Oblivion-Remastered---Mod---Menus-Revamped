"""Where each source unit the tests read lives. Moving or splitting a file changes this table only.

A unit can span several files (a split file): read() joins them in order, so a marker search still finds
each function once.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

UNITS = {
    "picker": ["src/sorting/Picker.cpp"],
    "container": ["src/engine/ContainerMenu.cpp"],
    "gear-data": ["src/sorting/GearData.cpp"],
    "hover": ["src/hover/HoverFix.cpp"],
    "search": ["src/search/SearchState.cpp", "src/search/ItemNames.cpp", "src/search/SearchBar.cpp",
               "src/search/SearchInput.cpp", "src/search/ListFilter.cpp", "src/search/Search.cpp"],
    "keyboard-api": ["src/keyboard/SearchApi.cpp"],
    "keyboard-input": ["src/keyboard/KeyboardInput.cpp"],
    "input-driver": ["src/core/Input.cpp"],
    "stolen-filter": ["src/sorting/rules/StolenFilter.h"],
}

# Include folders for compiling extracted bodies (headers such as TransferHold.h, ListLifecycle.h).
INCLUDE_DIRS = ["src", "src/sorting/rules", "src/search"]

SORTING_ASSETS = "assets/Sorting"
GONE_PROBE = "src/hover/StolenProbe.cpp"


def paths(unit):
    return [ROOT / p for p in UNITS[unit]]


def read(unit, encoding="utf-8-sig"):
    return "\n".join(p.read_text(encoding=encoding) for p in paths(unit))


def include_flags():
    return [f"/I{ROOT / d}" for d in INCLUDE_DIRS]
