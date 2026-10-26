import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("stage", ROOT / "tools/stage_menus_revamped.py")
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)


class MigrationTests(unittest.TestCase):
    def test_import_preserves_configuration_and_custom_art(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            old, new = root / "plugins", root / "stage"
            old.mkdir()
            search = old / "MenuSearch.ini"
            search.write_text("[Bar]\nuX=345\n[Search]\nbMagic=0\n[Log]\nuLogLevel=1\n", encoding="utf-8")
            (old / "MenuSearchControllerKeyboard.ini").write_text("[SearchKeyboard]\nuHoldMs=650\n")
            (old / "SortingOverhaul.ini").write_text("[Controller]\nbReturnToListAfterFilter=0\n")
            art = old / "SortingOverhaul/Weapons-All.png"
            art.parent.mkdir()
            art.write_bytes(b"owner artwork")
            before = search.read_bytes()
            migration.stage(old, new)
            config = migration.read_ini(new / "MenusRevamped.ini")
            self.assertEqual(config["Bar"]["uX"], "345")
            self.assertFalse(config.has_section("Inventory"))  # old Bar-derived positions remain derived
            self.assertEqual(config["Search"]["bMagic"], "0")
            self.assertEqual(config["SearchKeyboard"]["uHoldMs"], "650")
            self.assertEqual(config["Controller"]["bReturnToListAfterFilter"], "0")
            self.assertEqual(config["Log"]["uLogLevel"], "3")
            self.assertEqual((new / "MenusRevamped/Sorting/Weapons-All.png").read_bytes(), art.read_bytes())
            self.assertEqual(search.read_bytes(), before)
            (new / "MenusRevamped.ini").write_text("; unified customization\n", encoding="utf-8")
            (new / "MenusRevamped/Sorting/Weapons-All.png").write_bytes(b"new customization")
            migration.stage(old, new)
            self.assertEqual((new / "MenusRevamped.ini").read_text(), "; unified customization\n")
            self.assertEqual((new / "MenusRevamped/Sorting/Weapons-All.png").read_bytes(), b"new customization")

    def test_existing_unified_wins(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            old, new = root / "plugins", root / "stage"
            old.mkdir()
            (old / "MenusRevamped.ini").write_bytes(b"; existing unified\n[Log]\nuLogLevel=3\n")
            (old / "MenusRevamped/Search").mkdir(parents=True)
            (old / "MenusRevamped/Search/SearchBar.png").write_bytes(b"unified art")
            (old / "MenuSearch").mkdir()
            (old / "MenuSearch/SearchBar.png").write_bytes(b"old art")
            migration.stage(old, new)
            self.assertEqual((new / "MenusRevamped.ini").read_bytes(), (old / "MenusRevamped.ini").read_bytes())
            self.assertEqual((new / "MenusRevamped/Search/SearchBar.png").read_bytes(), b"unified art")

    def test_fresh_defaults_and_case_insensitive_keys(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            old, new = root / "plugins", root / "stage"
            old.mkdir()
            (old / "MenuSearchControllerKeyboard.ini").write_text("[searchkeyboard]\nuholdms=800\n")
            migration.stage(old, new)
            config = migration.read_ini(new / "MenusRevamped.ini")
            self.assertEqual(config["Search"]["bInventory"], "1")
            self.assertEqual(config["SearchKeyboard"]["uHoldMs"], "800")
            self.assertTrue((new / "MenusRevamped/Keyboard/Parchment.png").exists())

    def test_never_writes_into_live_install(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaises(ValueError):
                migration.stage(root, root / "inside-live-install")


if __name__ == "__main__":
    unittest.main()
