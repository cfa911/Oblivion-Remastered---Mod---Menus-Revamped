"""Smooth settings migrate without replacing unified customization or originals."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("stage",ROOT/"tools/stage_menus_revamped.py")
stage=importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)

class SmoothMigration(unittest.TestCase):
    def test_existing_unified_keeps_values_and_imports_missing_smooth_keys(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);old=root/"plugins";new=root/"stage";old.mkdir()
            legacy=old/"SmoothMenus.ini"
            legacy.write_text("[Hover]\nbInventory=0\nbMagic=0\nuIntervalMs=333\n"
                              "[Guard]\nuLimit=1234567\n[Metrics]\nbEnabled=0\n")
            original=legacy.read_bytes()
            (old/"MenusRevamped.ini").write_text("; owner comment\n[Bar]\nuX=999\n"
                                               "[hover]\nuintervalms=250\n[Log]\nuLogLevel=3\n")
            stage.stage(old,new)
            result=stage.read_ini(new/"MenusRevamped.ini")
            hover=next(result[s] for s in result.sections() if s.casefold()=="hover")
            values={k.casefold():v for k,v in hover.items()}
            self.assertEqual(values["uintervalms"],"250")
            self.assertEqual(values.get("binventory"),"0")
            self.assertEqual(values.get("bmagic"),"0")
            self.assertEqual(result["Guard"]["uLimit"],"1234567")
            self.assertEqual(result["Bar"]["uX"],"999")
            self.assertIn("; owner comment",(new/"MenusRevamped.ini").read_text())
            self.assertEqual(legacy.read_bytes(),original)
            before=(new/"MenusRevamped.ini").read_bytes()
            stage.stage(old,new)
            self.assertEqual((new/"MenusRevamped.ini").read_bytes(),before)

if __name__=="__main__": unittest.main()
