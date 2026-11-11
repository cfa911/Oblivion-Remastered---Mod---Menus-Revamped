"""Custom pack builder window (owner 2026-11-11, decision 26): the run-time tab shows only run-time entries (the baked [Skin]
values sit with the baked colours), and every tab is split into subgroups by menu so the owner sees where a colour applies.
Tested on the model (no Tk): ColourModel.pages() -> [(tab label, [(group, [entries])])]; the window draws one heading per group."""
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
import custom_colours as cc  # noqa: E402
from pack_builder import model  # noqa: E402

with tempfile.TemporaryDirectory() as temp:
    m = model.ColourModel(Path(temp) / "none.ini")
    pages = m.pages() if hasattr(m, "pages") else []
    labels = [label for label, _ in pages]
    check(len(labels) == 3 and labels[0].startswith("Colours") and labels[1].startswith("Tabs") and labels[2].startswith("Skin"),
          f"three tabs: baked colours, baked tabs, run-time skin ({labels})")
    shown = {}
    for label, groups in pages:
        for group, entries in groups:
            check(bool(group) and bool(entries), f"{label}: group {group!r} has a heading and entries")
            for e in entries:
                shown.setdefault((e.section, e.name), []).append((label, group))
    check(all(len(v) == 1 for v in shown.values()) and len(shown) == len(m.entries), "every entry appears exactly once")
    runtime = [e for _, groups in pages[2:3] for _, entries in groups for e in entries]
    check(runtime and all(e.applies == "Restart game" for e in runtime), "the run-time tab holds only run-time entries")
    baked_skin = [e for _, groups in pages[:1] for _, entries in groups for e in entries if e.section == "Skin"]
    check(sorted(e.name for e in baked_skin) == sorted(cc.BAKED_SKIN), f"the baked [Skin] values are on the baked colours tab ({[e.name for e in baked_skin]})")
    groups = {g for _, gs in pages for g, _ in gs}
    for g in ("Enchanting", "Spellmaking", "Alchemy"):
        check(any(g in name for name in groups), f"a {g} group exists ({sorted(groups)})")
    scroll = [g for (s, n), [(label, g)] in shown.items() if n == "scrollTrack"]
    check(scroll and "all menus" in scroll[0].lower(), f"the scrollbar sits in an all-menus group ({scroll})")
    check(all(e.name in cc.GROUPS or ("Skin." + e.name) in cc.GROUPS for e in m.entries) if hasattr(cc, "GROUPS") else False,
          "custom_colours.GROUPS names a group for every entry")

window = (root / "tools/pack_builder/window.py").read_text(encoding="utf-8")
check("pages()" in window and "heading" in window.lower(), "the window draws the model's pages with a heading per group")

print(f"{count} checks, {len(fails)} failures")
for why in fails:
    print("FAIL:", why)
sys.exit(1 if fails else 0)
