"""RMI-096: every popup on dark paper with light text in the Custom pack (owner 2026-11-13: "All popups now").

All popups draw M_Popup_PaperBackground (UI/Materials/Common/PopupPapers), which has no colour parameters or literals: its
colour is the two textures T_Paper_Wrinkled_D (paper) and T_Popup_Flowers_D (leaf border), both used by this material only,
so both are baked with the parchment recipe. The popup texts are blueprint literals (scan of the 11 popup roots, depth 1,
game 1.512.105.0; KB docs/reverse-engineering/dark-mode-ui.md "Popup paper"): stock ink and warm brown -> rowText, the blue
ones -> enchantedDot; the two popup-only rich-text tables' rows (offsets 69/425) -> rowText."""
import json
import sys
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

INK = [0.0723, 0.0382, 0.0284, 1.0]
BLUE = [0.0513, 0.0648, 0.1441, 1.0]
QUEST_BLUE = [0.0497, 0.0648, 0.147, 1.0]
WARM = [0.1195, 0.0666, 0.0343, 1.0]
WARM_DT = [0.1195, 0.0704, 0.0369, 1.0]

# package: (asset path under Content, [(offset, stock, colour)])
EXPECTED = {
    "WBP_ModernMenu_Quantity": ("UI/Modern/ModalLayer", [(12693, INK, "rowText"), (12793, INK, "rowText")]),
    "WBP_MessageMenu_Specific_Default": ("UI/Modern/ModalLayer/Messages", [(10765, INK, "rowText"), (11331, INK, "rowText")]),
    "WBP_ModernMenu_SleepWait": ("UI/Modern/ModalLayer", [(o, INK, "rowText") for o in (14108, 14158, 14208, 14274)]),
    "WBP_GenericMenu_Specific_SkillIncrease": ("UI/Modern/ModalLayer/Messages", [(5456, INK, "rowText"), (6105, INK, "rowText")]),
    "WBP_ModernMenu_QuestAdded": ("UI/Modern/GameMenuLayer/GenericMenu", [(11328, INK, "rowText")]),
    "WBP_ModernMenu_Negotiate": ("UI/Modern/ModalLayer", [
        (12756, INK, "rowText"), (12829, INK, "rowText"), (12928, BLUE, "enchantedDot"), (12994, BLUE, "enchantedDot"),
        (13064, INK, "rowText"), (13167, INK, "rowText"), (13235, INK, "rowText"), (13309, INK, "rowText")]),
    "WBP_Modern_SkillsMenu_SkillsAttributesSelection": (
        "UI/Modern/GameMenuLayer/SkillAndClassMenus/SkillsMenu/SkillsAttributesSelection",
        [(11327, WARM, "rowText"), (11426, INK, "rowText")]),
    "WBP_Modern_SettingsMenu_TimedRevertPopup": ("UI/Modern/MenuLayer/Settings/Popup",
                                                 [(o, INK, "rowText") for o in (8035, 8149, 8257)]),
    "WBP_Modern_SettingsMenu_ValidationPopup": ("UI/Modern/MenuLayer/Settings/Popup", [(8589, INK, "rowText"), (8698, INK, "rowText")]),
    "WBP_Modern_Settings_SaveLoadPopup": ("UI/Modern/MenuLayer/Settings/Prefabs", [(5480, INK, "rowText")]),
    "WBP_Modern_Onboarding_EULAScreen": ("UI/Modern/MenuLayer/Onboarding/Flow",
                                         [(o, INK, "rowText") for o in (17386, 17495, 17595, 17695, 17792)]),
    "WBP_ModernMenu_MapQuestEntry_NoFocus": ("UI/Modern/GameMenuLayer/GenericMenu",
                                             [(3457, INK, "rowText"), (3940, INK, "rowText"), (4010, QUEST_BLUE, "enchantedDot")]),
    "WBP_Modern_HorizontalSliderWithLabels": ("UI/Modern/Prefabs", [(2445, INK, "rowText"), (2512, INK, "rowText")]),
    "DT_MessageMenu_RichTextStyle": ("UI/Modern/ModalLayer/Messages", [(69, INK, "rowText"), (425, INK, "rowText")]),
    "DT_UI_SkillsClass_Birthsign_RichTextStyle": ("UI/Modern/GameMenuLayer/SkillAndClassMenus/DataTables/RichTextStyles",
                                                  [(69, WARM_DT, "rowText"), (425, WARM_DT, "rowText")]),
}
PAPER = ["Art/UI/Materials/Textures/T_Paper_Wrinkled_D", "UI/Materials/Common/PopupPapers/T_Popup_Flowers_D"]

data = json.loads((ROOT / "tools/menus/common.json").read_text(encoding="utf-8"))
assets = {a["asset"]: a for a in data["assets"]}
bps = data.get("blueprint_colours", {})

# the two material textures are no longer baked: the popups draw T_UI_BackgroundHorizontal (popup_paper_swap_tests)

for package, (folder, entries) in EXPECTED.items():
    got = [(e.get("offset"), e.get("stock"), e.get("colour")) for e in bps.get(package, [])]
    check(got == entries, f"{package}: popup text colours {entries} in common.json ({got})")
    row = assets.get(f"{folder}/{package}")
    check(row is not None and row["recipe"] == f"bp_colour:{package}"
          and sorted(row["colours"]) == sorted({c for _, _, c in entries}),
          f"{package}: asset row with recipe bp_colour:{package} at {folder}")
    check(pipe.BLUEPRINT_COLOURS.get(package) == [(o, tuple(s), c) for o, s, c in entries],
          f"{package}: the pipeline loads exactly these literals (no other data file overrides the package)")

# T_Paper_Wrinkled_D is PF_G8 (DXGI 61, R8_UNORM, sRGB default, 512x512, grey 187-242). Owner 2026-11-13 ("Keep stock
# grain"): parchment's dark..light stretch made the faint creases harsh (1.3x -> 4x); paper_scale darkens evenly instead:
# every value times parchmentLight / the texture's 98th-percentile brightness, so the grain keeps its stock ratio.
def r8_dds(width, height, pixels):
    head = bytearray(148)
    head[0:4], head[4:8] = b"DDS ", (124).to_bytes(4, "little")
    head[12:16], head[16:20] = height.to_bytes(4, "little"), width.to_bytes(4, "little")
    head[84:88], head[128:132] = b"DX10", (61).to_bytes(4, "little")
    return bytes(head) + bytes(pixels)


try:
    import tempfile
    pixels = [187 + (i * 55) // 255 for i in range(256)] * 4   # 32x32, the stock range 187..242
    stock = r8_dds(32, 32, pixels)
    check(pipe.texture_header(stock) == 148, "an R8 DX10 texture has a 148-byte header")
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "T_Paper_Wrinkled_D.dds"
        path.write_bytes(stock)
        check(pipe.format_supported("paper_scale", stock), "paper_scale accepts an R8 (PF_G8) texture")
        numbers, colours, fn = pipe.recipe("paper_scale")
        check(colours == ("parchmentLight",), f"paper_scale reads parchmentLight only ({colours})")
        out = fn(path, {"parchmentDark": [8.0] * 3, "parchmentLight": [33.0] * 3}, Path(tmp), None)
        check(len(out) == len(stock) and out[:148] == stock[:148], "R8 paper_scale keeps size and header")
        body = out[148:]
        check(32 <= max(body) <= 34, f"the paper's light end lands on parchmentLight ({max(body)})")
        check(abs(max(body) / max(min(body), 1) - 242 / 187) < 0.08,
              f"the grain keeps its stock ratio, not stretched ({min(body)}..{max(body)}, stock 187..242)")
        check(all(body[i] <= body[i + 1] for i in range(255)), "the paper's grain (brightness order) is kept")
except Exception as error:   # noqa: BLE001
    check(False, f"parchment recolours an R8 paper ({type(error).__name__}: {error})")

# a package in two data files would be silently overwritten by load_menus
menus = [json.loads(p.read_text(encoding="utf-8")) for p in (ROOT / "tools/menus").glob("*.json")]
for package in EXPECTED:
    owners = [m["menu"] for m in menus if package in m.get("blueprint_colours", {})]
    check(owners == ["Shared art"], f"{package}: in one data file only ({owners})")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
