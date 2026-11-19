"""Keyboard runtime colours survive the builder's ordinary edit/save/reopen path."""
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from pack_builder.model import ColourModel

names = ("keyboardKey", "keyboardEdge", "keyboardSelected", "keyboardSelectedEdge", "keyboardInk")
failures = []
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder) / "CustomColours.ini"
    model = ColourModel(path)
    groups = dict(dict(model.pages())["Runtime"])
    keyboard = next((entries for title, entries in groups.items() if title.startswith("Controller keyboard")), [])
    if {e.name for e in keyboard} != set(names):
        failures.append("Runtime has a separate controller keyboard group with all five colours")
    for name in names:
        try:
            entry = model.entry("Skin", name)
        except (KeyError, StopIteration):
            failures.append(f"{name}: editable colour is missing")
            continue
        if entry.applies != "Restart game":
            failures.append(f"{name}: save/restart applies the change")
        model.set_text("Skin", name, "0.1, 0.2, 0.3, 0.8")
    model.save()
    reopened = ColourModel(path)
    for name in names:
        try:
            if reopened.value("Skin", name) != [0.1, 0.2, 0.3, 0.8]:
                failures.append(f"{name}: RGBA edit survives Save/reopen")
        except (KeyError, StopIteration):
            pass
    reopened.reset_to_dark()
    for name in names:
        try:
            if reopened.value("Skin", name) == [0.1, 0.2, 0.3, 0.8]:
                failures.append(f"{name}: Reset restores the Dark default")
        except (KeyError, StopIteration):
            pass
for failure in failures:
    print("FAIL", failure)
print(f"keyboard builder: {len(failures)} failures")
sys.exit(bool(failures))
