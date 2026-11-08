"""Mouse movement over popup cells updates their highlight without changing controller focus."""
from pathlib import Path
import source_paths

root = Path(__file__).resolve().parents[1]
input_source = (root / "src/sorting/PickerInput.cpp").read_text()
tick_source = (root / "src/sorting/PickerTick.cpp").read_text()
popup_source = (root / "src/sorting/PickerPopup.cpp").read_text()
internal = (root / "src/sorting/PickerInternal.h").read_text()
message = source_paths.function(input_source, "bool Picker::Message(")
tick = source_paths.function(tick_source, "void Picker::Tick(")
paint = source_paths.function(popup_source, "void Picker::PaintController(")

checks = {
    "mouse movement hit-tests popup cells": "WM_MOUSEMOVE" in message and "cellsPx[i].Hit" in message,
    "mouse hover is published as shared state": "mouseHoverCell.store" in message and "mouseHoverCell" in internal,
    "closed popup clears hover": "if (!open)" in tick and "mouseHoverCell.store(-1" in tick,
    "hover changes repaint on the game tick": "mouseHoverCell.load" in tick and "paintedMouseHoverCell" in tick and "PaintController()" in tick,
    "popup paint combines hover and controller focus": "mouseHoverCell.load" in paint and "controllerPopup" in paint and "cursor" in paint,
}
failed = [name for name, passed in checks.items() if not passed]
for name in failed:
    print(f"FAIL {name}")
if failed:
    raise SystemExit(1)
print(f"{len(checks)} picker mouse-hover checks passed")
