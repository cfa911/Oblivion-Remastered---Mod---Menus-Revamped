"""Focused filename/default-generator contract; does not touch installed artwork."""
import importlib.util
from pathlib import Path
import unittest
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("icons", ROOT / "tools/generate_sorting_icons.py")
icons = importlib.util.module_from_spec(spec)
spec.loader.exec_module(icons)


class IconNames(unittest.TestCase):
    def test_alchemy_names(self):
        names = {f"{group}-{name}.png" for group, choices in icons.GROUPS.items() for name in choices}
        for name in ("Alchemy-All.png", "Potion-All.png", "Category-Alchemy.png"):
            self.assertIn(name, names)
            self.assertTrue((ROOT / source_paths.SORTING_ASSETS / name).is_file())
        self.assertNotIn("Potions-All.png", names)
        self.assertNotIn("Category-Potions.png", names)

    def test_potion_poison_groups_have_own_files(self):
        # Owner 2026-10-30: every potion/poison group has an editable file of its own.
        names = {f"{group}-{name}.png" for group, choices in icons.GROUPS.items() for name in choices}
        own = ("Alchemy-Poisons.png", "Potion-All.png", "Potion-Restore.png", "Potion-Fortify.png", "Potion-Ward.png",
               "Potion-Stealth.png", "Potion-Utility.png", "Potion-Cure.png", "Potion-Other.png",
               "Poison-All.png", "Poison-Damage.png", "Poison-Elemental.png", "Poison-Disable.png")
        for name in own:
            with self.subTest(name=name):
                self.assertIn(name, names)
                self.assertTrue((ROOT / source_paths.SORTING_ASSETS / name).is_file())

    def test_new_defaults_render(self):
        for group, name in (("Alchemy", "All"), ("Potion", "All"), ("Category", "Alchemy")):
            with self.subTest(group=group):
                self.assertEqual(icons.icon(group, name).size, (128, 128))


if __name__ == "__main__":
    unittest.main()
