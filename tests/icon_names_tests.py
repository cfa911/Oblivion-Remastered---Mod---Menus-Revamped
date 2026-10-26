"""Focused filename/default-generator contract; does not touch installed artwork."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("icons", ROOT / "tools/generate_sorting_icons.py")
icons = importlib.util.module_from_spec(spec)
spec.loader.exec_module(icons)


class IconNames(unittest.TestCase):
    def test_alchemy_names(self):
        names = {f"{group}-{name}.png" for group, choices in icons.GROUPS.items() for name in choices}
        for name in ("Alchemy-All.png", "Potion-All.png", "Category-Alchemy.png"):
            self.assertIn(name, names)
            self.assertTrue((ROOT / "sortingoverhaul/assets/SortingOverhaul" / name).is_file())
        self.assertNotIn("Potions-All.png", names)
        self.assertNotIn("Category-Potions.png", names)

    def test_new_defaults_render(self):
        for group, name in (("Alchemy", "All"), ("Potion", "All"), ("Category", "Alchemy")):
            with self.subTest(group=group):
                self.assertEqual(icons.icon(group, name).size, (128, 128))


if __name__ == "__main__":
    unittest.main()
