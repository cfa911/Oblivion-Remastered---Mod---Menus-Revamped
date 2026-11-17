"""RMI-108 (startup crash, KB bugs/menus-revamped/RMI-108-startup-crash-async-load-write.md): the Magic separator's baked
Visibility must use the cooked field the stock game uses for WBP_OriginalImageTile_C (52), never 62."""
import struct
import sys
import unittest

import source_paths

ROOT = source_paths.ROOT
sys.path.insert(0, str(ROOT / "tools"))
from pack_builder import pipeline

VISIBILITY = 52        # Default__WBP_OriginalImageTile_C stores 52; stock instances store ESlateVisibility 4/2/1/0 there
HIGHEST_STOCK = 53     # the highest field any of the 785 stock WBP_OriginalImageTile_C exports stores


def separator_rewrite():
    rows = [r for r in pipeline.LAYOUT["WBP_ModernMenu_MagicMenu"]["rewrites"] if r["widget"].startswith("HeaderList_Separator")]
    assert len(rows) == 1, rows
    return rows[0]


class CookedIndexTests(unittest.TestCase):
    def test_separator_visibility_is_field_52(self):
        r = separator_rewrite()
        self.assertEqual(r["add_bytes"], [[VISIBILITY, "01"]])   # Collapsed
        self.assertEqual(r["field_sizes"][VISIBILITY], 1)
        self.assertLessEqual(len(r["field_sizes"]) - 1, HIGHEST_STOCK)

    def test_separator_export_gains_only_field_52(self):
        r = separator_rewrite()
        header = pipeline.build_header(sorted(set(r["stock_fields"]) | {VISIBILITY}))
        fields, zeroes, _ = pipeline.parse_header(header + bytes(4), 0)
        self.assertEqual(fields, [4, 12, 35, 45, 52])
        self.assertEqual(zeroes, set())


if __name__ == "__main__":
    unittest.main()
