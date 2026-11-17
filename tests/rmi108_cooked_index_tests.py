"""RMI-108 (startup crash, KB bugs/menus-revamped/RMI-108-startup-crash-async-load-write.md): the Magic separator's baked
Visibility wrote cooked field 62 (the real field is 52). Since RMI-111 (owner 2026-11-17) the separator is not baked at all:
it is the sort bar's upper line. No layout rewrite may touch it again."""
import sys
import unittest

import source_paths

ROOT = source_paths.ROOT
sys.path.insert(0, str(ROOT / "tools"))
from pack_builder import pipeline


class CookedIndexTests(unittest.TestCase):
    def test_no_separator_rewrite(self):
        for asset, recipe in pipeline.LAYOUT.items():
            rows = [r for r in recipe.get("rewrites", []) if r["widget"].startswith("HeaderList_Separator")]
            self.assertEqual(rows, [], asset)

    def test_no_rewrite_adds_field_62(self):
        for asset, recipe in pipeline.LAYOUT.items():
            for r in recipe.get("rewrites", []):
                self.assertNotIn(62, [index for index, _ in r.get("add_bytes", [])], (asset, r["widget"]))


if __name__ == "__main__":
    unittest.main()
