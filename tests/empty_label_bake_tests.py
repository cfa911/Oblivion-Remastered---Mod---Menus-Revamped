"""RMI-095: the "Empty" list label's brown band (T_Empty_Background_D, 512x32) baked neutral grey in the Custom pack (owner
2026-11-13: "Neutral grey band", the highlighted-row grey listHighlight; shape and light edges kept). Shared by Inventory,
Container, Barter, Magic, Spell Purchase, Quests and Character (WBP_List_EmptyLabel).

Stock (exported 2026-11-13): band body (76, 56, 49) Rec.709 brightness 59.75, edges (214, 195, 173), alpha 163-203. Recipe
neutral:<key>:<stock body brightness>: every BC3 colour endpoint becomes grey, brightness scaled so the stock body lands on the
key's brightness (indices and alpha blocks kept, so the edges stay lighter and the shape stays)."""
import json
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


ROOT = source_paths.ROOT
sys.path.insert(0, str(ROOT / "tools"))
from pack_builder import pipeline as pipe  # noqa: E402

data = json.loads((ROOT / "tools/menus/common.json").read_text(encoding="utf-8"))
row = [a for a in data["assets"] if a["asset"] == "Art/UI/Common/T_Empty_Background_D"]
check(len(row) == 1 and row[0]["recipe"] == "neutral:listHighlight:59.75" and row[0]["colours"] == ["listHighlight"]
      and len(row[0]["shared_with"]) >= 6, "T_Empty_Background_D is baked neutral to listHighlight (shared list named)")

try:
    from PIL import Image
    numbers, colours, fn = pipe.recipe("neutral:listHighlight:59.75")
    check(colours == ("listHighlight",), "neutral reads its colour key")
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        stock = work / "T_Empty_Background_D.dds"
        image = Image.new("RGBA", (64, 32), (76, 56, 49, 185))
        for x in range(64):
            for y in (0, 1):
                image.putpixel((x, y), (214, 195, 173, 203))
        image.save(stock, pixel_format="DXT5")
        stock_alpha = [p[3] for p in Image.open(stock).convert("RGBA").get_flattened_data()]
        (work / "out.dds").write_bytes(fn(stock, {"listHighlight": [91.0, 91.0, 91.0]}, work, None))
        out = Image.open(work / "out.dds").convert("RGBA")
        body, edge = out.getpixel((32, 16)), out.getpixel((32, 0))
        check(max(body[:3]) - min(body[:3]) <= 4 and abs(body[0] - 91) <= 6, f"the band body becomes the highlight grey ({body})")
        check(max(edge[:3]) - min(edge[:3]) <= 4 and edge[0] > body[0] + 60, f"the edges stay lighter, neutral ({edge})")
        check([p[3] for p in out.get_flattened_data()] == stock_alpha, "the alpha (the band's shape) is kept byte for byte")
except Exception as error:   # noqa: BLE001
    check(False, f"neutral recolours the band ({error})")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
