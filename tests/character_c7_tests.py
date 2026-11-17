"""Step C.7 Character (owner batch 2026-11-16): text colours, dividers, icons, flourishes and faction icons in Custom."""
import struct
import sys
import tempfile
import unittest

from PIL import Image

import source_paths

ROOT = source_paths.ROOT
sys.path.insert(0, str(ROOT / "tools"))
from pack_builder import pipeline

GOLD, BLACK, BLUE = (232, 187, 102), (16, 16, 16), (79, 124, 176)
CHARACTER_ICONS = "Art/UI/Icons/Icon_Character/T_CharacterIcon_"
ATTRIBUTE_ICONS = "Art/UI/Icons/Dynamic_Icons/menus/level_up/attributes_icons/T_attributes_icon_"
RANK_ICONS = "Art/UI/Icons/Dynamic_Icons/menus/Icons/ndquest/"
RING = "Art/UI/Common/T_UI_CircularProgress"
SKILL_CARDS = "Art/UI/Icons/Dynamic_Icons/menus/class/attributes/T_load_image_"
SKILLS = (
    "acrobatics", "alchemy", "alteration", "armorer", "athletics", "blade", "block", "blunt",
    "conjuration", "destruction", "hand_to_hand", "heavy_armor", "illusion", "light_armor",
    "Marksman", "mercantile", "mysticism", "restoration", "security", "sneak", "speechcraft",
)

# {package: {.uexp offset: colour}} - offsets from the stock scan (KB mods/menus-revamped/menus/Character/values.md)
WRITERS = {
    "WBP_ModernMenu_StatsPage1": {12213: "menuTitle", 13353: "menuTitle", 13412: "menuTitle"},
    "WBP_ModernMenu_StatsPlayerInfo": {3703: "rowText", 3774: "rowText", 4219: "menuTitle", 4271: "menuTitle"},
    "WBP_ModernMenu_StatsPrimaryAttribute": {6306: "rowText", 6376: "rowText", 6534: "rowText"},
    "WBP_ModernMenu_StatsSecondaryAttribute": {6241: "rowText", 6291: "rowText", 6605: "rowText", 6621: "statusRed",
                                               6360: "menuTitle", 6416: "menuTitle"},
    "WBP_ModernMenu_StatsPage3": {17892: "rowText", 17957: "rowText"},
    "WBP_ModernMenu_StatsSkillEntry": {4880: "menuTitle", 4931: "rowText", 5000: "rowText", 5732: "menuTitle",
                                       5801: "menuTitle", 5854: "menuTitle"},
    "WBP_ModernMenu_StatsFactionEntry": {2768: "effectIcon", 2818: "white", 3203: "menuTitle", 3256: "menuTitle"},
    "WBP_LegacyMenu_StatsMiscEntry": {1909: "rowText", 2020: "rowText", 2093: "menuTitle", 2149: "menuTitle"},
    "DT_StatsMenu_SkillDescriptionRichTextStyle_MainDescription": {425: "accentValue"},
    # shared with the Inventory/Magic details (owner: "this is the idea")
    "WBP_ModernMenu_Stats_LevelDescription": {5415: "effectIcon"},
    "DT_StatsMenu_SkillDescriptionRichTextStyle_PerLevelDescription": {425: "accentValue"},
}


def tiny_dds(folder, pixels, size):
    """An uncompressed BGRA DDS (Pillow) holding the given RGBA pixels, row-major."""
    image = Image.new("RGBA", size)
    image.putdata(pixels)
    path = folder / "stock.dds"
    image.save(path)
    return path


class CharacterTests(unittest.TestCase):
    def test_character_and_attribute_icons_become_gold_ink_on_black(self):
        names = [CHARACTER_ICONS + n for n in ("birthsign", "class", "fatigue", "health", "levelUp", "magicka", "race")]
        names += [ATTRIBUTE_ICONS + n for n in ("agility", "endurance", "intelligence", "luck", "personality", "speed",
                                                "strength", "willpower")]
        for name in names:
            with self.subTest(name=name):
                self.assertEqual(pipeline.TEXTURES.get(name), "ink_gold")
        self.assertFalse([a for a in pipeline.TEXTURES if a.startswith("ArtOriginal/") and "attributes_icon" in a],
                         "the Stats page draws the Art/UI copies")
        self.assertEqual(pipeline.recipe("ink_gold")[1], ("effectIcon", "frame"))

    def test_ink_gold_fills_the_holes_and_keeps_the_margin_clear(self):
        clear, ink, tile = (0, 0, 0, 0), (0, 0, 0, 0), (120, 80, 60, 255)
        # 4x2: column 0 is an empty margin, columns 1-3 the tile with one see-through ink pixel at (2, 0)
        pixels = [clear, tile, ink, tile, clear, tile, tile, tile]
        with tempfile.TemporaryDirectory() as folder:
            work = source_paths.Path(folder)
            stock = tiny_dds(work, pixels, (4, 2))
            baked = work / "baked.dds"
            baked.write_bytes(pipeline.checked_texture(stock.read_bytes(), pipeline.ink_gold_texture(stock, GOLD, BLACK, work)))
            out = pipeline.decode_rgba(baked)
            self.assertEqual(out.getpixel((2, 0)), GOLD + (255,))
            self.assertEqual(out.getpixel((1, 0)), BLACK + (255,))
            self.assertEqual(out.getpixel((0, 0))[3], 0)
            self.assertEqual(out.getpixel((0, 1))[3], 0)

    def test_title_flourish_is_gold_and_rank_icons_get_a_white_backing(self):
        self.assertEqual(pipeline.TEXTURES.get("Art/UI/Common/T_PauseMenuDeco"), "laurel_tint:effectIcon")
        for name in ("T_ndpilgrim", "T_ndknighterrant", "T_ndknightcommander", "T_ndholycrusader"):
            with self.subTest(name=name):
                self.assertEqual(pipeline.TEXTURES.get(RANK_ICONS + name), "white_backing")
        self.assertEqual(pipeline.TEXTURES.get(RANK_ICONS + "T_iconndivines"), "quest_icon")

    def test_white_backing_composites_translucent_art_over_white(self):
        pixels = [(0, 0, 0, 0), (200, 0, 0, 255), (0, 0, 0, 128), (10, 20, 30, 255)]
        with tempfile.TemporaryDirectory() as folder:
            work = source_paths.Path(folder)
            stock = tiny_dds(work, pixels, (2, 2))
            baked = work / "baked.dds"
            baked.write_bytes(pipeline.checked_texture(stock.read_bytes(), pipeline.white_backing_texture(stock, work)))
            out = pipeline.decode_rgba(baked)
            self.assertEqual(out.getpixel((0, 0)), (255, 255, 255, 255))
            self.assertEqual(out.getpixel((1, 0)), (200, 0, 0, 255))
            self.assertTrue(all(abs(c - 127) <= 2 for c in out.getpixel((0, 1))[:3]))
            self.assertEqual(out.getchannel("A").getextrema(), (255, 255))

    def test_character_text_and_dividers_use_the_custom_roles(self):
        values = {"rowText": [239] * 3, "menuTitle": list(BLUE), "accentValue": list(BLUE), "effectIcon": list(GOLD),
                  "Skin.emphasisInk": [32, 31, 32]}
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

    def test_attribute_red_is_a_brighter_fixed_red(self):
        red = pipeline.CONSTANT_COLOURS["statusRed"]
        stock = [pipeline.srgb(v) for v in (0.3467, 0.0262, 0.0262)]
        self.assertGreater(red[0], stock[0] + 30)
        self.assertGreater(red[0], 2.5 * red[1])

    def test_character_packages_that_layout_resizes_keep_their_colour_recipe(self):
        for package in ("WBP_ModernMenu_StatsSkillEntry", "WBP_ModernMenu_StatsPage3"):
            with self.subTest(package=package):
                self.assertIn(package, pipeline.LAYOUT)
                self.assertEqual(pipeline.TEXTURES.get(pipeline.LAYOUT[package]["asset"]), f"bp_colour:{package}")

    def test_skill_panel_lines_are_white_and_title_ornaments_gold(self):
        expected = {
            "WBP_ModernMenu_StatsPage3_Details": {
                24431: "effectIcon", 24519: "effectIcon", 24643: "white", 24761: "white",
            },
            "WBP_ModernMenu_Stats_LevelDescription": {5651: "white", 5704: "white"},
        }
        for package, roles in expected.items():
            with self.subTest(package=package):
                rows = pipeline.BLUEPRINT_COLOURS[package]
                stock = bytearray(max(offset for offset, _, _ in rows) + 16)
                for offset, rgba, _ in rows:
                    struct.pack_into("<4f", stock, offset, *rgba)
                values = {"effectIcon": GOLD, "rowText": (239,) * 3, "Skin.ornament": (0.3,) * 4}
                baked = pipeline.bp_colour(stock, package, values)
                for offset, role in roles.items():
                    with self.subTest(offset=offset):
                        rgba = struct.unpack_from("<4f", baked, offset)
                        self.assertEqual(tuple(round(pipeline.srgb(c)) for c in rgba[:3]),
                                         GOLD if role == "effectIcon" else (255,) * 3)
                        self.assertEqual(rgba[3], 1.0)

    def test_skill_ring_material_uses_the_blue_accent(self):
        name = "MIC_UI_SkillCircularProgress"
        recipe = pipeline.recipe(f"material_colour:{name}")
        self.assertEqual(recipe[1], ("menuTitle",))
        stock = bytearray(185)
        struct.pack_into("<4f", stock, 169, 0.86316, 0.3564, 0.13014, 1.0)
        baked = recipe[2](stock, {"menuTitle": BLUE}, None, None)
        rgba = struct.unpack_from("<4f", baked, 169)
        self.assertEqual(tuple(round(pipeline.srgb(c)) for c in rgba[:3]), BLUE)
        self.assertEqual(rgba[3], 1.0)

    def test_both_skill_art_sizes_and_shared_ring_are_selected(self):
        self.assertEqual(pipeline.TEXTURES.get(RING), "skill_ring")
        expected = {}
        for slot, skill in enumerate(SKILLS):
            expected[SKILL_CARDS + skill] = f"skill_card:{slot}"
            expected[SKILL_CARDS + skill.lower() + "_small"] = f"skill_icon:{slot}"
        selected = {asset: recipe for asset, recipe in pipeline.TEXTURES.items()
                    if recipe.partition(":")[0] in ("skill_card", "skill_icon")}
        self.assertEqual(selected, expected)

    def test_built_skill_art_retains_colour_and_only_large_paper_is_transparent(self):
        with tempfile.TemporaryDirectory() as folder:
            work = source_paths.Path(folder)
            shade = (180, 150, 130)
            values = {"frame": BLACK}
            for name, size, format_code, subject in (
                ("skill_card:1", (256, 512), "DXT5", (128, 100)),
                ("skill_icon:1", (64, 64), "DXT1", (32, 18)),
                ("skill_icon:0", (128, 128), "DXT1", (64, 40)),
            ):
                with self.subTest(name=name):
                    stock = work / "stock.dds"
                    Image.new("RGB", size, shade).save(stock, pixel_format=format_code)
                    asset_kind = name.partition(":")[0]
                    self.assertTrue(pipeline.format_supported(asset_kind, stock.read_bytes()))
                    result = work / "baked.dds"
                    result.write_bytes(pipeline.checked_texture(
                        stock.read_bytes(), pipeline.recipe(name)[2](stock, values, work, None)))
                    image = pipeline.decode_rgba(result)
                    self.assertTrue(all(abs(a - b) <= 9 for a, b in zip(image.getpixel(subject), shade)))
                    paper = image.getpixel((0, 0))
                    if asset_kind == "skill_card":
                        self.assertEqual(paper[3], 0)
                        self.assertEqual(image.getpixel(subject)[3], 255)
                    else:
                        self.assertTrue(all(abs(a - b) <= 9 for a, b in zip(paper, BLACK)))
                        self.assertEqual(paper[3], 255)

    def test_skill_ring_colours_the_disc_and_outer_ring_gold(self):
        stock = Image.new("RGB", (32, 32), (150,) * 3)
        baked = pipeline.skill_ring_image(stock)
        for point in ((16, 16), (16, 1)):
            red, green, blue = baked.getpixel(point)
            self.assertTrue(red > green > blue)
        self.assertEqual(baked.getpixel((0, 0)), stock.getpixel((0, 0)))
        self.assertGreater(baked.getpixel((8, 8))[0], baked.getpixel((23, 23))[0])

if __name__ == "__main__":
    unittest.main()
