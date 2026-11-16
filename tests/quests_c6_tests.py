"""Quests uses the Inventory search style and bakes its local text writers."""
import configparser
import re
import struct
import sys
import tempfile
import unittest

from PIL import Image

import source_paths

ROOT = source_paths.ROOT
sys.path.insert(0, str(ROOT / "tools"))
from pack_builder import pipeline


class QuestTests(unittest.TestCase):
    def test_quest_title_row_and_inventory_search_style(self):
        config = configparser.ConfigParser()
        config.read(ROOT / "config/MenusRevamped.defaults.ini", encoding="utf-8-sig")
        position = tuple(config["Quest"].getfloat(key) for key in ("uX", "uY", "uWidth", "uHeight"))
        self.assertEqual(position, (508, 269, 392, 36))
        body = source_paths.function(source_paths.read("search"), "const uistyle::Search& BarStyle")
        predicate = re.search(r"SearchFor\((.*?), preset\)", body, re.DOTALL).group(1)
        self.assertIn("a_menu == menus::Menu::kQuests", predicate)

    def test_quest_search_availability_follows_the_list_instead_of_inspect(self):
        search = source_paths.read("search")
        tick = source_paths.function(search, "void Tick(menus::Menu a_menu)")
        self.assertIn("quests::ListPanel()", tick)
        self.assertIn("BarTick(menus::Menu::kQuests, now);", tick.split("if (g_typing.load())")[0])
        for function in ("menus::Menu Context()", "bool BarMessage(", "void BarTick("):
            with self.subTest(function=function):
                self.assertIn("g_questSearchShown", source_paths.function(search, function))

    def test_quest_paper_has_independent_bakes(self):
        quest_art = "Art/UI/Modern/GameMenuLayer/Quest/"
        for texture in ("QuestPage/T_QuestPage_PaperBackground", "QuestHistory/T_QuestHistory_FramedBackground2",
                        "QuestHistory/T_QuestHistory_PaperBackground"):
            with self.subTest(texture=texture):
                self.assertEqual(pipeline.TEXTURES.get(quest_art + texture), "paper_invert:1.5:0.7")
        shared = "Art/UI/Modern/GameMenuLayer/magic/T_MagicBackground"
        self.assertEqual(pipeline.TEXTURES[shared], "page_invert:3:0.7")

    def test_tabs_use_matching_paper_and_draw_over_it_in_custom_only(self):
        # Round four (owner 2026-11-16): tabs on top of the parchment. Slots 5,6,7,4,3 -> 5,4,6,7,3 (paper before the tab
        # switcher and inputs), colour pack only: Classic/Light (MRLayout_P) keep the stock order.
        self.assertNotIn("WBP_Modern_QuestListPanel", pipeline.LAYOUT)
        rows = {r["offset"]: (r.get("type"), r["stock"], r["value"])
                for r in pipeline.BLUEPRINT_LITERALS.get("WBP_Modern_QuestListPanel", [])}
        self.assertEqual(rows, {36: ("int32", 6, 4), 40: ("int32", 7, 6), 44: ("int32", 4, 7)})
        for state in ("Available", "Completed"):
            name = f"Art/UI/Modern/GameMenuLayer/Quest/QuestPage/T_QuestPage_{state}QuestTabSelected"
            self.assertEqual(pipeline.TEXTURES[name], "charcoal_paper:quests")

    def test_both_quest_divider_textures_use_effect_gold(self):
        root = "Art/UI/Modern/GameMenuLayer/Quest/QuestPage/"
        for name in ("T_QuestPage_ListViewItem_Decorator", "T_QuestPage_DecoratedSeparator"):
            self.assertEqual(pipeline.TEXTURES.get(root + name), "laurel_tint:effectIcon")

    def test_icon_alpha_becomes_blue_over_gold_and_spares_gold_art(self):
        icons = {name.rsplit("/", 1)[-1]: recipe for name, recipe in pipeline.TEXTURES.items()
                 if recipe == "quest_icon"}
        self.assertEqual(set(icons) & {"T_iconndivines", "T_icon_settlements_quest_icon", "T_icon_mythic_dawnGold"},
                         {"T_iconndivines", "T_icon_settlements_quest_icon"})
        self.assertFalse(any("gold" in name.lower() for name in icons))
        self.assertEqual(pipeline.recipe("quest_icon")[1], ("menuTitle", "effectIcon"))

    def test_bgra_quest_icon_keeps_odd_dimensions_and_rejects_opaque_art(self):
        with tempfile.TemporaryDirectory() as folder:
            work = source_paths.Path(folder)
            stock = work / "quest-icon.dds"
            blue, gold = (79, 124, 176), (232, 187, 102)
            icon = Image.new("RGBA", (5, 3), (0, 0, 0, 0))
            icon.putpixel((0, 0), (255, 255, 255, 255))
            icon.save(stock)
            data = stock.read_bytes()
            self.assertTrue(pipeline.format_supported("quest_icon", data))
            baked = work / "baked.dds"
            baked.write_bytes(pipeline.checked_texture(data, pipeline.quest_icon_texture(stock, blue, gold, work)))
            actual = pipeline.decode_rgba(baked)
            self.assertEqual(actual.size, icon.size)
            self.assertEqual(actual.getpixel((0, 0)), blue + (255,))
            self.assertEqual(actual.getpixel((1, 0)), gold + (255,))
            self.assertEqual(actual.getchannel("A").getextrema(), (255, 255))
            icon.putalpha(255)
            icon.save(stock)
            with self.assertRaises(pipeline.SkipAsset):
                pipeline.quest_icon_texture(stock, blue, gold, work)

    def test_quest_preview_recipes_and_baked_flowers_stay_local(self):
        prefix = "Art/UI/Modern/GameMenuLayer/Quest/"
        assets = {
            "QuestPage/T_QuestEntry_FocusMarker": "laurel_tint:listHighlight:255",
            "QuestPage/T_QuestPage_ActiveQuest_Icon": "laurel_tint:effectIcon",
            "QuestPage/T_QuestPage_ActiveQuest_Decorator": "laurel_tint:effectIcon:42",
            "QuestHistory/T_QuestHistory_SelectedDarkBackgroundFull": "flat:accentValue",
        }
        for ornament in ("FlowerDecoration_Left_TODELETE", "FlowerDecoration_Right_TODELETE", "DecoratedSeparator",
                         "Separator", "VerticalSeparator_Left", "VerticalSeparator_Right"):
            assets["QuestHistory/T_QuestHistory_" + ornament] = "laurel_tint:effectIcon"
        for asset, recipe in assets.items():
            with self.subTest(asset=asset):
                self.assertEqual(pipeline.TEXTURES.get(prefix + asset), recipe)
                self.assertEqual(pipeline.recipe(recipe)[1], ("listHighlight",) if "FocusMarker" in asset else
                                 ("accentValue",) if "BackgroundFull" in asset else ("effectIcon",))
        swaps = pipeline.IMPORT_SWAPS.get("WBP_Modern_QuestHistoryPanel", [])
        for side in ("Left", "Right"):
            name = "T_QuestHistory_FlowerDecoration_" + side
            self.assertNotIn(prefix + "QuestHistory/" + name, pipeline.TEXTURES)
            self.assertIn((name, name + "_TODELETE"), [(r["from_object"], r["to_object"]) for r in swaps])

    def test_quest_text_writers_and_layout_can_share_the_row_package(self):
        writers = {
            "WBP_Modern_QuestEntry": {6740: "rowText"},
            "WBP_Modern_QuestListPanel": {13617: "rowText", 13743: "menuTitle"},
            "WBP_Modern_QuestDetailPanel": {8524: "rowText", 9146: "rowText", 9199: "rowText", 9299: "menuTitle"},
            "WBP_Modern_QuestHistoryPanel": {20624: "menuTitle"},
            "WBP_Modern_QuestHistoryCarouselEntry": {11268: "rowText", 11735: "rowText", 11855: "rowText"},
            "DT_QuestMenu_RichTextStyle_DetailsPanel": {69: "rowText", 425: "Skin.emphasisInk"},
            "DT_QuestMenu_RichTextStyle_CarouselEntry": {69: "rowText", 425: "Skin.emphasisInk"},
        }
        for package, expected in writers.items():
            with self.subTest(package=package):
                rows = pipeline.BLUEPRINT_COLOURS.get(package, [])
                actual = {offset: role for offset, _, role in rows}
                self.assertEqual({offset: actual.get(offset) for offset in expected}, expected)
                before = bytearray(max(expected) + 16)
                for offset, stock, _ in rows:
                    struct.pack_into("<4f", before, offset, *stock)
                for literal in pipeline.BLUEPRINT_LITERALS.get(package, []):
                    struct.pack_into("<i", before, literal["offset"], literal["stock"])
                after = pipeline.bp_colour(before, package, {"rowText": [239] * 3, "menuTitle": [119] * 3,
                                                             "Skin.emphasisInk": [220, 187, 92]})
                for offset in expected:
                    self.assertNotEqual(after[offset:offset + 12], before[offset:offset + 12])
                    self.assertEqual(after[offset + 12:offset + 16], before[offset + 12:offset + 16])
        self.assertIn("WBP_Modern_QuestEntry", pipeline.LAYOUT)
        self.assertEqual(pipeline.TEXTURES.get(pipeline.LAYOUT["WBP_Modern_QuestEntry"]["asset"]),
                         "bp_colour:WBP_Modern_QuestEntry")


ROUND4 = source_paths.Path("F:/modbuild/reports/quests-c6")
OWNER_INI = source_paths.Path("F:/Games/The Elder Scrolls IV - Oblivion Remastered/OblivionRemastered/Binaries/Win64/OBSE/"
                              "Plugins/MenusRevamped/CustomColours.ini")


@unittest.skipUnless((ROUND4 / "round-4-preview").is_dir() and OWNER_INI.is_file(), "owner preview files absent")
class QuestRound4Tests(unittest.TestCase):
    """Round four (owner-approved previews): parchments at blur 1.5; tabs shaded in float, edge-blended, encoded once."""

    @classmethod
    def setUpClass(cls):
        import custom_colours
        cls.values = custom_colours.pack_values(OWNER_INI)
        cls.page = ROUND4 / "T_QuestPage_PaperBackground-stock.dds"

    def bake(self, recipe, stock):
        _, _, build = pipeline.recipe(recipe)
        with tempfile.TemporaryDirectory() as folder:
            return build(stock, self.values, source_paths.Path(folder), lambda name: self.page)

    def test_parchments_match_the_approved_dds(self):
        for asset in ("T_QuestPage_PaperBackground", "T_QuestHistory_FramedBackground2", "T_QuestHistory_PaperBackground"):
            with self.subTest(asset=asset):
                stock = ROUND4 / (asset + "-stock.dds")
                actual = pipeline.checked_texture(stock.read_bytes(), self.bake("paper_invert:1.5:0.7", stock))
                self.assertEqual(actual, (ROUND4 / "round-4-preview" / (asset + "-1.5.dds")).read_bytes())

    def test_tabs_match_the_approved_dds(self):
        for state in ("Available", "Completed"):
            with self.subTest(state=state):
                asset = f"T_QuestPage_{state}QuestTabSelected"
                stock = ROUND4 / "round-3-preview" / (asset + "-stock.dds")
                actual = pipeline.checked_texture(stock.read_bytes(), self.bake("charcoal_paper:quests", stock))
                # Round five (owner-approved 2026-11-16): round four's tab with the bottom 12 rows' alpha faded to 0.
                self.assertEqual(actual, (ROUND4 / "round-5-preview" / (asset + "-proposed.dds")).read_bytes())


if __name__ == "__main__":
    unittest.main()
