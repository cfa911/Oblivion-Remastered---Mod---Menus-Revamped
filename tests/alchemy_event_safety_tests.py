"""RMI-022: the Alchemy watchers must not rebuild lists inside the game's own ProcessEvent.

A filter click runs the menu's UpdateItemsList; re-sorting the ingredient list from the post-call watcher (ClearListItems /
AddItem while the game's update is still on the stack) released row widgets the game still used: "Pure virtual function
being called" (owner 2026-11-04). The watcher only marks work; the next tick does it.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

src = source_paths.read("alchemy")
checks = failures = 0


def check(ok, why):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print("FAIL", why)


start = src.find("void OnMenuEvent(")
body = src[start:src.find("\n\t\t}\n", start)]
check(start >= 0, "menu watcher found")
check("ApplySort(" not in body and "SetItems(" not in body and "ComputeCounts(" not in body,
      "the menu watcher does not sort, rebuild or read lists inside the game's call")
check("g_dirtySort = true" in body, "it marks the sort for the next tick")
check("off += 8" not in src, "no scan of a filter item for text-shaped fields (RMI-022: it called ToString on garbage)")
safe = src[src.find("bool SafeText("):]
safe = safe[:safe.find("\n\t\t}\n")]
check("InImage(" in safe, "every text read checks the text object's vtable lies in the game image first")
deco = src[src.find("void DecorateRow("):]
deco = deco[:deco.find("\n\t\t}\n")]
check("uix::SetText(text," not in deco and "WithCount(" not in deco,
      "RMI-024: the row's own effect text is never rewritten (its selected look depends on it); the count has its own label")
rows = src[src.find("void DecorateRows("):]
rows = rows[:rows.find("\n\t\t}\n")]
restore = src[src.find("void RestoreSelected("):]
restore = restore[:restore.find("\n\t\t}\n")]
check("RestoreSelected(row" in rows and "ShouldRestoreSelected(" in restore and '"Checkmark"' in restore and '"Selected_State"' in restore
      and "SetVisibility" in restore,
      "RMI-024: the tick gives reused rows of active filters their selected look back")
ev = src[src.find("void OnEntryEvent("):]
ev = ev[:ev.find("\n\t\t}\n")]
check('"Checkmark"' not in ev and "ShouldRestoreSelected(" not in ev,
      "RMI-024: the restore is not done inside the game's own row call")
print(f"{checks} checks, {failures} failures")
sys.exit(1 if failures else 0)
