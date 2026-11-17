"""Help menu round 1 (owner batch 2026-11-17, KB mods/menus-revamped/menus/Help/CHECKLIST.md H.1-H.10): baked colours."""
import sys
import unittest

import source_paths

ROOT = source_paths.ROOT
sys.path.insert(0, str(ROOT / "tools"))
from pack_builder import pipeline

HELP = "UI/Modern/GameMenuLayer/Help/"
INK = (0.0723, 0.0382, 0.0284, 1.0)
BODY = (0.0976, 0.0545, 0.0395, 1.0)


def colours(package):
    return {offset: (stock, name) for offset, stock, name in pipeline.BLUEPRINT_COLOURS.get(package, [])}


class HelpColourTests(unittest.TestCase):
    def test_entry_header_band_text_triangle_and_names(self):
        entry = colours("WBP_ModernMenu_HelpEntry")
        self.assertEqual(entry.get(9342), ((0.2918, 0.159, 0.0908, 0.5), "menuTitle"))   # H.1 band blue
        self.assertEqual(entry.get(8986), (INK, "effectIcon"))                           # H.1 header text gold
        self.assertEqual(entry.get(9247), (INK, "effectIcon"))                           # H.1 triangle gold
        self.assertEqual(entry.get(9390), ((1.0, 1.0, 1.0, 0.6), "white"))               # H.2 focus band
        self.assertEqual(pipeline.BLUEPRINT_SCALES.get(("WBP_ModernMenu_HelpEntry", 9390)), 0.45)
        self.assertEqual(entry.get(9038), (INK, "rowText"))                              # H.5 names white
        self.assertEqual(pipeline.TEXTURES.get(HELP + "WBP_ModernMenu_HelpEntry"), "bp_colour:WBP_ModernMenu_HelpEntry")

    def test_help_titles_texts_and_counter(self):
        help_ = colours("WBP_ModernMenu_Help")
        expected = {44438: (INK, "menuTitle"),    # H.4 Tutorials
                    44371: (INK, "menuTitle"),    # H.6 summary title
                    44221: (INK, "menuTitle"),    # H.6 details title
                    44116: (BODY, "rowText"),     # H.8 summary text
                    43477: (BODY, "rowText"),     # H.9 details default colour
                    44299: (INK, "rowText")}      # H.10 counter
        self.assertEqual(help_, expected)
        self.assertEqual(pipeline.TEXTURES.get(HELP + "WBP_ModernMenu_Help"), "bp_colour:WBP_ModernMenu_Help")

    def test_rich_style_rows_are_light(self):
        rows = colours("DT_HelpMenuRichStyle")
        self.assertEqual(rows, {69: (BODY, "rowText"), 425: (INK, "rowText"), 781: (INK, "rowText")})   # H.9
        self.assertEqual(pipeline.TEXTURES.get(HELP + "data/DT_HelpMenuRichStyle"), "bp_colour:DT_HelpMenuRichStyle")

    def test_separator_gold_and_row_highlight_dimmer(self):
        art = "Art/UI/Modern/GameMenuLayer/Help/"
        self.assertEqual(pipeline.TEXTURES.get(art + "T_UI_HelpSeparator"), "flat:effectIcon")   # H.7
        # H.3 (owner: every list menu): the shared list highlight keeps listHighlight at 35 % of its stock alpha
        self.assertEqual(pipeline.TEXTURES.get("Art/UI/Common/T_List_highlight_V3"), "flat_alpha:listHighlight:0.35")


if __name__ == "__main__":
    unittest.main()
