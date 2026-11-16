"""Spellmaking Effects search must use the native left list and the shared input path."""
import unittest

import source_paths


class SpellmakingSearchTests(unittest.TestCase):
    def test_spellmaking_uses_the_shared_filter_on_the_left_list(self):
        search = source_paths.read("search")
        self.assertIn("FilterTick(menus::Menu::kSpellmaking, now)", search)
        self.assertIn('"SpellMaking_ListView"', search)
        self.assertIn('L"/Script/Altar.LegacyEffectsItemProperties"', search)
        self.assertIn('"NameText"', search)

    def test_effect_search_and_spell_name_have_separate_contexts(self):
        search = source_paths.read("search")
        context = source_paths.function(search, "menus::Menu Context()")
        self.assertIn("menus::Menu::kSpellmaking", context)
        self.assertLess(context.index("enchanting::NamingMenu()"), context.index("menus::Menu::kSpellmaking"))
        config = source_paths.ROOT / "src/search/SearchConfig.cpp"
        self.assertIn('L"bSpellmaking"', config.read_text(encoding="utf-8"))

    def test_bar_sits_between_the_title_and_existing_sort(self):
        search = source_paths.read("search")
        self.assertIn('"SpellMaking_TabName"', search)
        self.assertIn('"SpellMaking_Filters"', search)
        self.assertIn('B.hint = "Search effects"', search)


if __name__ == "__main__":
    unittest.main()
