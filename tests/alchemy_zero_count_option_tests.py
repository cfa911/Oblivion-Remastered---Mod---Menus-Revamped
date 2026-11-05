"""IMP-MR-004: the native Alchemy filter list never contains an effect no carried ingredient has.

Owner test 2026-11-05 (dev DLL 5bd500a8): no "(0)" row on All, Positive or Negative, even after dropping every
Chameleon ingredient (the row disappeared). bHideZeroCountEffects therefore never changed anything and is removed.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

root = Path(__file__).resolve().parents[1]
checks = failures = 0


def check(ok, why):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print("FAIL", why)


defaults = (root / "config/MenusRevamped.defaults.ini").read_text(encoding="utf-8-sig")
config = (root / "src/core/Config.cpp").read_text(encoding="utf-8-sig") + (root / "src/core/Config.h").read_text(encoding="utf-8-sig")
alchemy = source_paths.read("alchemy")

check("bHideZeroCountEffects" not in defaults, "defaults INI no longer offers bHideZeroCountEffects")
check("HideZeroCountEffects" not in config and "hideZeroCountEffects" not in config, "config no longer reads the switch")
check("hideZeroCountEffects" not in alchemy, "Alchemy menu no longer filters zero-count rows itself")
check("[Alchemy]" in defaults and "uDefaultSort=" in defaults, "the [Alchemy] section and its other option stay")
print(f"{checks} checks, {failures} failures")
sys.exit(1 if failures else 0)
