"""Character skill-card colour masks and the gold skill level ring (owner batch 2026-11-17)."""
import struct
import sys
import unittest

from PIL import Image

import source_paths

ROOT = source_paths.ROOT
sys.path.insert(0, str(ROOT / "tools"))
from pack_builder import pipeline

ATLAS = ROOT / "tools" / "menus" / "character-skill-masks.png"
BLUE = (79, 124, 176)

# {atlas slot: card points (x, y) in the owner's circled areas that must keep the stock illustration}
OWNER_AREAS = {
    3: [(133, 50), (185, 106)],              # armorer: the flag at the top
    5: [(110, 400)],                         # blade: lower guard and grip
    6: [(219, 259), (146, 454)],             # block: right edge of the burst, bottom of the shield
    7: [(128, 56), (70, 90)],                # blunt: axe head glow (top half)
    11: [(125, 63), (180, 420), (22, 283)],  # heavy armor: helmet top, gauntlets, left edge of the burst
    13: [(138, 54), (81, 60)],               # light armor: top rim above the emblem
    15: [(128, 24), (40, 300)],              # mercantile: top sunburst, lower sides
    18: [(128, 436), (100, 450)],            # security: bottom of the key bits
    19: [(128, 446)],                        # the chameleon card (texture T_load_image_sneak): its outer rim
    20: [(40, 130), (40, 380), (220, 130)],  # speechcraft: outer ring and its corner ornaments
}


def illustration(slot):
    row, col = divmod(slot, 7)
    with Image.open(ATLAS) as atlas:
        return atlas.crop((col * 256, row * 640, col * 256 + 256, row * 640 + 512)).getchannel("L")


class SkillCardMaskTests(unittest.TestCase):
    def test_owner_circled_areas_keep_the_stock_illustration(self):
        for slot, points in OWNER_AREAS.items():
            mask = illustration(slot)
            for point in points:
                with self.subTest(slot=slot, point=point):
                    self.assertGreaterEqual(mask.getpixel(point), 128)

    def test_card_corners_stay_paper(self):
        for slot in OWNER_AREAS:
            mask = illustration(slot)
            for point in ((4, 4), (251, 4), (4, 507), (251, 507)):
                with self.subTest(slot=slot, point=point):
                    self.assertEqual(mask.getpixel(point), 0)


class SkillRingTests(unittest.TestCase):
    def test_disc_and_outer_ring_are_gold(self):
        stock = Image.new("RGB", (32, 32), (150,) * 3)
        baked = pipeline.skill_ring_image(stock)
        for point in ((16, 16), (16, 1)):
            red, green, blue = baked.getpixel(point)
            self.assertTrue(red > green > blue, baked.getpixel(point))
        self.assertEqual(baked.getpixel((0, 0)), stock.getpixel((0, 0)))

    def test_progress_arc_uses_the_blue_accent(self):
        name = "MIC_UI_SkillCircularProgress"
        recipe = pipeline.recipe(f"material_colour:{name}")
        self.assertEqual(recipe[1], ("menuTitle",))
        stock = bytearray(185)
        struct.pack_into("<4f", stock, 169, 0.86316, 0.3564, 0.13014, 1.0)
        baked = recipe[2](stock, {"menuTitle": BLUE}, None, None)
        rgba = struct.unpack_from("<4f", baked, 169)
        self.assertEqual(tuple(round(pipeline.srgb(c)) for c in rgba[:3]), BLUE)


if __name__ == "__main__":
    unittest.main()
