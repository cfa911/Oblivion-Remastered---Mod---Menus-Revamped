"""Step C.8b (owner batch 2026-11-17): the Repair paper without its cloudy stains, the scrollbar track light grey and the
thumb a white outline around the search-bar grey, and the list header / details separators (T_Line_Brown) in the
column-divider grey."""
import sys
import tempfile
import unittest

from PIL import Image

import source_paths

ROOT = source_paths.ROOT
sys.path.insert(0, str(ROOT / "tools"))
import custom_colours as cc
from pack_builder import pipeline

TRACK = ("Art/UI/Common/Scrollbar/NewTextures/T_UI_Scrollbar_VertTop_D",
         "Art/UI/Common/Scrollbar/NewTextures/T_UI_Scrollbar_VertMiddleNew_D",
         "Art/UI/Common/Scrollbar/NewTextures/T_UI_Scrollbar_VertBtm_D")
SEPARATOR = "Art/UI/Common/T_Line_Brown"
REPAIR = "Art/UI/Modern/GameMenuLayer/Repair/T_repair_frame"


def stock_image(folder, image):
    path = folder / "stock.dds"
    image.save(path)
    return path


def paper_sides(baked_bytes, folder):
    path = folder / "baked.dds"
    path.write_bytes(baked_bytes)
    out = pipeline.decode_rgba(path)
    y = out.height // 2
    return out.getpixel((4, y))[0], out.getpixel((out.width - 5, y))[0]


class C8bTests(unittest.TestCase):
    def test_track_textures_are_white_so_the_track_tint_alone_sets_the_colour(self):
        for asset in TRACK:
            with self.subTest(asset=asset):
                self.assertEqual(pipeline.TEXTURES.get(asset), "flat_white")
        self.assertEqual(pipeline.recipe("flat_white")[1], ())

    def test_flat_white_whitens_the_black_rings_and_keeps_alpha(self):
        with tempfile.TemporaryDirectory() as folder:
            work = source_paths.Path(folder)
            image = Image.new("RGBA", (4, 4))
            image.putdata([(0, 0, 0, 255), (255, 255, 255, 128), (0, 0, 0, 0), (90, 60, 30, 255)] * 4)
            stock = stock_image(work, image)
            baked = pipeline.recipe("flat_white")[2](stock, {}, work, None)
            path = work / "white.dds"
            path.write_bytes(baked)
            out = pipeline.decode_rgba(path)
            self.assertEqual(out.getpixel((0, 0)), (255, 255, 255, 255))
            self.assertEqual(out.getpixel((1, 0))[3], 128)
            self.assertEqual(out.getpixel((2, 0))[3], 0)
            self.assertEqual(out.getpixel((3, 0))[:3], (255, 255, 255))

    def test_scrollbar_defaults_track_grey_white_outline_search_bar_fill(self):
        self.assertEqual(cc.SKIN["scrollTrack"], 0.6445)    # sRGB 210, a little greyer than the outline
        self.assertEqual(cc.SKIN["scrollBorder"], 0.8632)   # sRGB 239, the thumb's former white
        self.assertEqual(cc.SKIN["scrollInside"], 0.025)    # the Inventory/Magic search bar fill (UiTheme kInventorySkinSearch)

    def test_header_and_details_separators_use_the_column_divider_grey(self):
        self.assertEqual(pipeline.TEXTURES.get(SEPARATOR), "flat:columnLine")

    def test_repair_paper_recipe_removes_the_cloudy_stains(self):
        self.assertEqual(pipeline.TEXTURES.get(REPAIR), "altar_panel_invert:enchantingFrame:1.5:12:25")

    def test_clean_flattens_a_large_stain_and_plain_invert_keeps_it(self):
        image = Image.new("RGBA", (64, 64))
        image.putdata([((200,) * 3 if x < 32 else (175,) * 3) + (255,) for y in range(64) for x in range(64)])
        with tempfile.TemporaryDirectory() as folder:
            work = source_paths.Path(folder)
            stock = stock_image(work, image)
            plain = paper_sides(pipeline.altar_panel_texture(stock, (67, 75, 91), None, work, None, (60.0, 1.5)), work)
            clean = paper_sides(pipeline.altar_panel_texture(stock, (67, 75, 91), None, work, None, (60.0, 1.5, 12.0)), work)
        self.assertGreater(abs(plain[0] - plain[1]), 10, plain)
        self.assertLessEqual(abs(clean[0] - clean[1]), 3, clean)

    def test_stain_allowance_keeps_brownish_stains_as_paper(self):
        # a brownish stain (lum ~190, channel spread 38) beside clean paper: without the allowance it blends toward the metal
        image = Image.new("RGBA", (64, 64))
        image.putdata([((205, 203, 200) if x < 32 else (212, 190, 174)) + (255,) for y in range(64) for x in range(64)])
        metal = (67, 75, 91)
        with tempfile.TemporaryDirectory() as folder:
            work = source_paths.Path(folder)
            stock = stock_image(work, image)
            plain = paper_sides(pipeline.altar_panel_texture(stock, metal, None, work, None, (20.0, 1.5, 12.0)), work)
            allowed = paper_sides(pipeline.altar_panel_texture(stock, metal, None, work, None, (20.0, 1.5, 12.0, 25.0)), work)
        self.assertGreater(abs(plain[0] - plain[1]), 10, plain)
        self.assertLessEqual(abs(allowed[0] - allowed[1]), 3, allowed)
        _, needs, _ = pipeline.recipe("altar_panel_invert:enchantingFrame:1.5:12:25")
        self.assertEqual(needs, ("enchantingFrame", "panel", "parchmentDark", "parchmentLight"))

    def test_stain_allowance_keeps_the_frame_edge_next_to_dark_metal(self):
        # owner 2026-11-17 ("lost the edge of the frame"): a light, brownish ridge band beside the dark frame keeps its metal
        # colour; the same colour far inside the paper is a stain and becomes paper
        ridge, paper, dark = (212, 190, 174), (205, 203, 200), (60, 55, 50)
        image = Image.new("RGBA", (96, 64))
        image.putdata([(dark if x < 16 else ridge if x < 22 else ridge if 70 <= x < 80 else paper) + (255,)
                       for y in range(64) for x in range(96)])
        metal = (67, 75, 91)
        with tempfile.TemporaryDirectory() as folder:
            work = source_paths.Path(folder)
            stock = stock_image(work, image)
            path = work / "baked.dds"
            path.write_bytes(pipeline.altar_panel_texture(stock, metal, None, work, None, (20.0, 1.5, 0.0, 25.0)))
            out = pipeline.decode_rgba(path)
        edge, inner, clear = out.getpixel((19, 32)), out.getpixel((75, 32)), out.getpixel((50, 32))
        self.assertGreater(edge[2] - edge[0], 8, edge)       # the ridge stays steel (blue-grey), not the neutral paper
        self.assertLessEqual(max(inner[:3]) - min(inner[:3]), 2, (inner, clear))   # the inner stain is neutral paper, not steel

    def test_header_rule_is_the_column_divider_grey(self):
        # owner 2026-11-17: the line under the column icons is our own header rule (run time); it and the header's
        # vertical dividers take the list column-divider grey 99 (linear 0.1248), opaque
        self.assertEqual(cc.SKIN["rule"], [0.1248, 0.1248, 0.1248, 1.0])

    def test_repair_anvil_effect_colour_is_light_steel_blue(self):
        # RMI-110: M_UI_Repair's orange effect colour drew brown spots on the steel anvil
        self.assertEqual(pipeline.TEXTURES.get("UI/Materials/Repair/M_UI_Repair"), "material_colour:M_UI_Repair")
        rows = pipeline.BLUEPRINT_COLOURS.get("M_UI_Repair", [])
        self.assertEqual(sorted((offset, colour) for offset, _, colour in rows), [(588, "steelGlow"), (10719, "steelGlow")])
        self.assertEqual(pipeline.CONSTANT_COLOURS.get("steelGlow"), [193.0, 215.0, 255.0])


if __name__ == "__main__":
    unittest.main()
