"""Step C.8 Repair + Recharge (owner batch 2026-11-16): titles, rows and right-panel text, the selected-row band, the
right parchment and the smith frames in steel (owner chose option A, the Enchanting frame metal)."""
import struct
import sys
import tempfile
import unittest

from PIL import Image

import source_paths

ROOT = source_paths.ROOT
sys.path.insert(0, str(ROOT / "tools"))
from pack_builder import pipeline

STEEL = (67, 75, 91)
LEGACY_UI = source_paths.Path("F:/modbuild/reports/mr-tab-outlines-20261109/ui-all/OblivionRemastered/Content")
FOCUS_BOX = "UI/Legacy/Prefabs/WBP_LegacyPrefab_FocusBox"

TEXTURES = {
    "Art/UI/Modern/GameMenuLayer/Repair/T_repair_frame": "altar_panel_invert:enchantingFrame:1.5:12:25",   # C.8b: stains cleaned
    "Art/UI/Modern/GameMenuLayer/Repair/T_Repair_Hammer_Frame": "altar_metal:enchantingFrame",
    "Art/UI/Modern/GameMenuLayer/Recharge/T_UI_Recharge_Frame_No_Glow": "frame_metal:enchantingFrame",
    "Art/UI/Modern/GameMenuLayer/Recharge/T_UI_Paper_Background": "paper_invert:3",
    "Art/UI/Modern/GameMenuLayer/Recharge/T_goldframe": "altar_metal:enchantingFrame",
    "Art/UI/Modern/GameMenuLayer/Recharge/T_pattern_2": "flat:pattern",
    "Art/UI/Modern/GameMenuLayer/SigilStone/T_SigilStone_IconFrame": "altar_metal:enchantingFrame",
}
# round 2 (owner 2026-11-16): the action buttons in the same steel, the Recharge button gem stays green
BUTTONS = {
    "Art/UI/Modern/GameMenuLayer/Repair/T_UI_Repair": "frame_metal:enchantingFrame",
    "Art/UI/Modern/GameMenuLayer/Recharge/T_UI_Frame_Recharge": "frame_metal:enchantingFrame",
}

# {package: {.uexp offset: colour}} - offsets from the stock scan (KB mods/menus-revamped/menus/Repair|Recharge/values.md)
WRITERS = {
    "WBP_ModernMenu_RepairMenu": {
        67275: "menuTitle", 67098: "rowText", 67437: "rowText", 67536: "rowText", 67639: "rowText", 67738: "rowText",
        67807: "rowText", 67903: "rowText", 68114: "rowText", 68181: "rowText",
    },
    "WBP_LegacyMenu_RepairEntry": {3061: "rowText", 3129: "rowText"},
    "WBP_ModernMenu_Recharge": {
        63711: "menuTitle", 63127: "rowText", 63224: "rowText", 63291: "rowText", 63368: "rowText", 63462: "rowText",
        63567: "rowText", 63634: "rowText",
    },
    "WBP_LegacyMenu_RechargeItem": {5246: "rowText", 5320: "rowText", 5387: "rowText"},
}


def tiny_dds(folder, pixels, size):
    image = Image.new("RGBA", size)
    image.putdata(pixels)
    path = folder / "stock.dds"
    image.save(path)
    return path


class RepairRechargeTests(unittest.TestCase):
    def test_right_panel_art_uses_the_steel_frame_and_parchment_recipes(self):
        for asset, recipe in TEXTURES.items():
            with self.subTest(asset=asset):
                self.assertEqual(pipeline.TEXTURES.get(asset), recipe)
        self.assertEqual(pipeline.recipe("frame_metal:enchantingFrame")[1], ("enchantingFrame",))

    def test_action_buttons_are_steel_and_the_button_gem_stays_stock(self):
        for asset, recipe in BUTTONS.items():
            with self.subTest(asset=asset):
                self.assertEqual(pipeline.TEXTURES.get(asset), recipe)
        self.assertFalse([a for a in pipeline.TEXTURES if a.endswith("/T_UI_Gem_Recharge")])

    def test_frame_metal_recolours_the_metal_and_keeps_green_and_blue_gems(self):
        metal, green, blue, clear = (150, 120, 90, 255), (20, 160, 60, 255), (30, 80, 200, 255), (0, 0, 0, 0)
        with tempfile.TemporaryDirectory() as folder:
            work = source_paths.Path(folder)
            stock = tiny_dds(work, [metal, green, blue, clear], (2, 2))
            baked = work / "baked.dds"
            baked.write_bytes(pipeline.checked_texture(stock.read_bytes(), pipeline.frame_metal_texture(stock, STEEL, work)))
            out = pipeline.decode_rgba(baked)
            expected = tuple(round(c) for c in pipeline.altar_metal(metal[:3], STEEL))
            self.assertTrue(all(abs(a - b) <= 1 for a, b in zip(out.getpixel((0, 0))[:3], expected)))
            self.assertEqual(out.getpixel((1, 0)), green)
            self.assertEqual(out.getpixel((0, 1)), blue)
            self.assertEqual(out.getpixel((1, 1))[3], 0)

    def test_titles_rows_and_right_text_use_the_custom_roles(self):
        values = {"rowText": [239] * 3, "menuTitle": [79, 124, 176]}
        for package, expected in WRITERS.items():
            with self.subTest(package=package):
                rows = pipeline.BLUEPRINT_COLOURS.get(package, [])
                actual = {offset: role for offset, _, role in rows}
                self.assertEqual({offset: actual.get(offset) for offset in expected}, expected)
                before = bytearray(max(actual) + 16)
                for offset, stock, _ in rows:
                    struct.pack_into("<4f", before, offset, *stock)
                after = pipeline.bp_colour(before, package, values)
                for offset in expected:
                    self.assertNotEqual(after[offset:offset + 12], before[offset:offset + 12])
                    self.assertEqual(after[offset + 12:offset + 16], before[offset + 12:offset + 16], "stock alpha kept")

    def test_legacy_focus_box_draws_the_baked_list_highlight(self):
        self.assertEqual(pipeline.TEXTURES.get(FOCUS_BOX), "bp_colour:WBP_LegacyPrefab_FocusBox")
        swaps = pipeline.IMPORT_SWAPS.get("WBP_LegacyPrefab_FocusBox")
        self.assertEqual([(s["from_object"], s["to_object"], s["to_class"]) for s in swaps or []],
                         [("T_LevelUp_Frame_Light", "T_List_highlight_V3", "Texture2D")])
        self.assertEqual(pipeline.TEXTURES.get("Art/UI/Common/T_List_highlight_V3"), "flat:listHighlight")
        stock = LEGACY_UI / (FOCUS_BOX + ".uasset")
        if not stock.is_file():
            self.skipTest("stock legacy UI export not present")
        swapped = pipeline.swap_imports(stock.read_bytes(), swaps)
        names = [n[0] for n in pipeline.package_summary(swapped)["names"]]
        self.assertIn("/Game/Art/UI/Common/T_List_highlight_V3", names)
        self.assertNotIn("T_LevelUp_Frame_Light", names)


if __name__ == "__main__":
    unittest.main()
