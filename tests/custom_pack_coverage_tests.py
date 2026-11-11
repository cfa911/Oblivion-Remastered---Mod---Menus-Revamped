"""Custom pack builder Phase 7: the coverage tool (tools/custom_pack_coverage.py) writes the KB coverage page from Dark Mode
UI's saved asset lists (names only; `retoc list` lines) and tools/menus/*.json, so each later phase reruns it:
- every listed Dark Mode UI asset appears exactly once, grouped by menu, with a status: in pack / later / not planned (reason);
- widget and data assets not in the pack are "later: literal patch only" (owner decision 20, no whole blueprints);
- a notes file adds reasons and "not planned" entries and the menu grouping rules;
- our own pack assets that Dark Mode UI does not touch are listed separately;
- the summary counts textures and widget/data assets in the pack; the output is deterministic."""
import json
import subprocess
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
tool = root / "tools/custom_pack_coverage.py"
check(tool.is_file(), "tools/custom_pack_coverage.py exists")


def line(pack, path):
    return f"{pack:<30} 0123456789abcdef00000001 ExportBundleData     ../../../OblivionRemastered/Content/{path}.uasset"


with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    textures = temp / "textures-list.txt"
    textures.write_text("\n".join([
        "-",
        line("DMUI_Textures_P", "Art/UI/Common/T_A"),
        line("DMUI_Textures_P", "Art/UI/Modern/GameMenuLayer/enchantment/T_Paper"),
        line("DMUI_Textures_P", "Art/UI/Modern/GameMenuLayer/stats/T_Stat"),
        line("DMUI_Textures_P", "Art/UI/Icons/Icon_Category/T_Category_WorldMap"),
    ]) + "\n", encoding="utf-8")
    widgets = temp / "text-edits-list.txt"
    widgets.write_text("\n".join([
        line("DMUI_TextEdits_P", "UI/Modern/GameMenuLayer/enchantment/WBP_Ench"),
        line("DMUI_TextEdits_P", "UI/Modern/GameMenuLayer/stats/WBP_Stats"),
    ]) + "\n", encoding="utf-8")
    menus = temp / "menus"
    menus.mkdir()
    (menus / "enchanting.json").write_text(json.dumps({"menu": "Enchanting", "order": 1, "assets": [
        {"asset": "Art/UI/Modern/GameMenuLayer/enchantment/T_Paper", "recipe": "parchment", "colours": [], "shared_with": []},
        {"asset": "UI/Modern/GameMenuLayer/enchantment/WBP_Ench", "recipe": "bp_colour:WBP_Ench", "colours": [], "shared_with": []},
        {"asset": "Art/UI/Modern/GameMenuLayer/enchantment/T_OurOwn", "recipe": "flat:x", "colours": [], "shared_with": []},
    ]}), encoding="utf-8")
    notes = temp / "coverage_notes.json"
    notes.write_text(json.dumps({
        "menus": [["/enchantment/", "Enchanting"], ["/stats/", "Stats"], ["Art/UI/Common/", "Shared art"], ["Icon_Category", "Shared icons"]],
        "not_planned": {"Art/UI/Icons/Icon_Category/T_Category_WorldMap": "no UI importer (loaded by code)"},
        "later": {"/stats/": "page shared with Help and Quest (decision 22)"},
    }), encoding="utf-8")
    out = temp / "custom-pack-coverage.md"

    def run():
        return subprocess.run([sys.executable, str(tool), "--textures", str(textures), "--widgets", str(widgets),
                               "--menus", str(menus), "--notes", str(notes), "--out", str(out)], capture_output=True, text=True)

    result = run() if tool.is_file() else None
    check(result is not None and result.returncode == 0, f"the tool runs ({result.stderr[-300:] if result else 'missing'})")
    text = out.read_text(encoding="utf-8") if out.is_file() else ""
    rows = [l for l in text.splitlines() if l.startswith("| `")]
    names = [l.split("`")[1] for l in rows]
    for name in ("T_A", "T_Paper", "T_Stat", "T_Category_WorldMap", "WBP_Ench", "WBP_Stats", "T_OurOwn"):
        check(names.count(name) == 1, f"{name} is listed exactly once ({names.count(name)})")

    def row(name):
        return next((l for l in rows if l.split("`")[1] == name), "")

    check("in pack" in row("T_Paper") and "parchment" in row("T_Paper"), f"an asset in the data files is 'in pack' with its recipe ({row('T_Paper')})")
    check("in pack" in row("WBP_Ench"), "a patched blueprint is 'in pack'")
    check("later" in row("WBP_Stats") and "literal" in row("WBP_Stats"), f"a widget not in the pack is 'later: literal patch only' ({row('WBP_Stats')})")
    check("not planned" in row("T_Category_WorldMap") and "loaded by code" in row("T_Category_WorldMap"), "not planned with its reason")
    check("later" in row("T_Stat") and "Help and Quest" in row("T_Stat"), "a later note's reason is shown")
    check("later" in row("T_A"), "anything else is 'later'")
    check("## Enchanting" in text and "## Stats" in text and "## Shared art" in text, "assets are grouped by menu")
    own = text.split("## Our own assets", 1)[-1] if "## Our own assets" in text else ""
    check("T_OurOwn" in own and "T_Paper" not in own, "our own assets Dark Mode UI does not touch are listed separately")
    check("1 of 4 textures" in text and "1 of 2 widget" in text, "the summary counts what is in the pack")
    first = text
    if tool.is_file():
        run()
    check(out.read_text(encoding="utf-8") == first if out.is_file() else False, "the output is deterministic")

# the real notes file ships next to the tool (not in tools/menus, which the builder reads)
real = root / "tools/coverage_notes.json"
check(real.is_file() and {"menus", "not_planned", "later"} <= set(json.loads(real.read_text(encoding="utf-8"))) if real.is_file() else False,
      "tools/coverage_notes.json holds the menu rules and the notes")
check(not (root / "tools/menus/coverage_notes.json").exists(), "the notes file is not a builder data file")

print(f"{count} checks, {len(fails)} failures")
for why in fails:
    print("FAIL:", why)
sys.exit(1 if fails else 0)
