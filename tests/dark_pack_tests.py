"""Dark UI pack phase 0 (owner 2026-11-10): the baked parchment pack, switched in or out at start-up by the INI preset."""
import re
import sys
import source_paths

fails = []
count = 0
def check(ok, why):
    global count
    count += 1
    if not ok:
        fails.append(why)

root = source_paths.ROOT
pack = root / "assets/DarkPack"
check(all((pack / f"MRDark_P.{ext}").is_file() for ext in ("pak", "utoc", "ucas")), "the Dark pack ships (MRDark_P .pak/.utoc/.ucas in assets/DarkPack)")
tool = root / "tools/make_dark_pack.py"
tool_text = tool.read_text(encoding="utf-8") if tool.is_file() else ""
check("T_SpellPurchase_LeftPageBG" in tool_text and "to-zen" in tool_text and "DarkStyle.h" in tool_text,
      "the pack is built by tools/make_dark_pack.py from the stock parchment and DarkStyle.h")

plugin = (root / "src/core/Plugin.cpp").read_text(encoding="utf-8")
load = plugin.split("OBSE_PLUGIN_LOAD", 1)[1] if "OBSE_PLUGIN_LOAD" in plugin else ""
check("darkpack::Apply()" in load and load.index("mrconfig::Load()") < load.index("darkpack::Apply()") < load.index("RegisterListener"),
      "the plugin switches the pack at load, after reading the INI (before the engine mounts paks: MR-PAK-PROBE)")
dp_path = root / "src/core/DarkPack.cpp"
dp = dp_path.read_text(encoding="utf-8") if dp_path.is_file() else ""
check("~mods" in dp and "MRDark_P" in dp and "Preset::Dark" in dp and "remove" in dp and "copy_file" in dp,
      "Dark copies the pack into Content/Paks/~mods, the other presets remove it")

skin = source_paths.read("picker")
page = source_paths.function(skin, "void StyleMagicPage(")
check("darkpack::Active()" in page, "with the pack active the Magic details overlay steps aside (the game's own material draws)")
details = source_paths.function(skin, "void StyleDetails(")
check("darkpack::Active()" in details, "with the pack active the Inventory details page tint is white (the parchment itself is dark)")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
