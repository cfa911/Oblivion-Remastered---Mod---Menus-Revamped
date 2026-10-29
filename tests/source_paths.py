"""Where each source unit the tests read lives. Moving or splitting a file changes this table only.

A unit can span several files (a split file): read() joins them in order, so a marker search still finds
each function once.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

UNITS = {
    "picker": ["src/sorting/PickerInternal.h", "src/sorting/Picker.cpp", "src/sorting/PickerWidgets.cpp",
               "src/sorting/ItemRows.cpp", "src/sorting/PickerHeader.cpp", "src/sorting/PickerPopup.cpp",
               "src/sorting/PickerTick.cpp", "src/sorting/CategoryEvaluator.cpp", "src/sorting/PickerInput.cpp",
               "src/sorting/NativeBackground.cpp"],
    "container": ["src/engine/ContainerMenu.cpp"],
    "gear-data": ["src/sorting/GearData.cpp"],
    "hover": ["src/hover/HoverFix.cpp"],
    "search": ["src/search/SearchState.cpp", "src/search/ItemNames.cpp", "src/search/SearchBar.cpp",
               "src/search/SearchInput.cpp", "src/search/ListFilter.cpp", "src/search/Search.cpp"],
    "plugin": ["src/core/Plugin.cpp"],
    "config-core": ["src/core/Config.cpp"],
    "config-search": ["src/search/SearchConfig.cpp"],
    "config-keyboard": ["src/keyboard/KeyboardConfig.cpp"],
    "keyboard-input": ["src/keyboard/KeyboardInput.cpp"],
    "input-driver": ["src/core/Input.cpp"],
    "stolen-filter": ["src/sorting/rules/StolenFilter.h"],
    "pe-hook": ["src/engine/PEHook.cpp"],
    "ui-call": ["src/engine/UiCall.cpp"],
}

# Include folders for compiling extracted bodies (headers such as TransferHold.h, ListLifecycle.h).
INCLUDE_DIRS = ["src", "src/sorting/rules", "src/search"]

# Helper definitions the own-call test extracts from UiCall.cpp besides the ui::Call bodies.
UI_CALL_EXTRA = ["\t\tbool ProcessEventGuarded("]

SORTING_ASSETS = "assets/Sorting"
GONE_PROBE = "src/hover/StolenProbe.cpp"


def paths(unit):
    return [ROOT / p for p in UNITS[unit]]


def read(unit, encoding="utf-8-sig"):
    return "\n".join(p.read_text(encoding=encoding) for p in paths(unit))


def function(text, head):
    """The definition starting with `head` (declarations ending in ';' are skipped), through its closing brace."""
    start = text.find(head)
    while start >= 0:
        brace, semicolon = text.find("{", start), text.find(";", start)
        if brace >= 0 and (semicolon < 0 or brace < semicolon):
            depth, end = 1, brace + 1
            while depth:
                depth += (text[end] == "{") - (text[end] == "}")
                end += 1
            return text[start:end]
        start = text.find(head, start + 1)
    return ""


def include_flags():
    return [f"/I{ROOT / d}" for d in INCLUDE_DIRS]
