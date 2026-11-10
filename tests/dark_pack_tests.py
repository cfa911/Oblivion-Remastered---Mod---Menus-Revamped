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
rules_path = root / "src/core/rules/DarkPackSwitch.h"
switch_text = dp + (rules_path.read_text(encoding="utf-8") if rules_path.is_file() else "")
check("~mods" in switch_text and "MRDark_P" in switch_text and "Preset::Dark" in dp and "remove" in switch_text and "copy_file" in switch_text,
      "Dark copies the pack into Content/Paks/~mods, the other presets remove it")

skin = source_paths.read("picker")
page = source_paths.function(skin, "void StyleMagicPage(")
check("darkpack::Active()" in page, "with the pack active the Magic details overlay steps aside (the game's own material draws)")
details = source_paths.function(skin, "void StyleDetails(")
check("darkpack::Active()" in details, "with the pack active the Inventory details page tint is white (the parchment itself is dark)")

# Phase 1 (owner 2026-11-10): the pack pipeline and the switch made release-grade.
import hashlib
import importlib.util
import json

check("--paks" in tool_text and "to-legacy" in tool_text and "--mode\", \"export\"" in tool_text,
      "make_dark_pack.py extracts the stock textures from the game's Paks itself (retoc to-legacy, UE4-DDS-Tools export)")
manifest_path = pack / "MRDark_P.json"
manifest = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.is_file() else {}
check(bool(manifest), "the pack has a build manifest (assets/DarkPack/MRDark_P.json) written by make_dark_pack.py")
style_text = (root / "src/core/DarkStyle.h").read_text(encoding="utf-8")
for name, value in manifest.get("style", {}).items():
    found = re.search(r"float\s+" + name + r"\s*=\s*([\d.]+)f?", style_text)
    check(found is not None and abs(float(found.group(1)) - value) < 1e-9,
          f"the pack was baked from the current DarkStyle.h {name} (rebuild the pack after a colour change)")
check(bool(manifest.get("style")), "the manifest records the DarkStyle.h values the pack was baked from")
for ext in ("pak", "utoc", "ucas"):
    path = pack / f"MRDark_P.{ext}"
    digest = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else ""
    check(manifest.get("files", {}).get(f"MRDark_P.{ext}") == digest, f"MRDark_P.{ext} is the file the manifest's build wrote")
check(set(manifest.get("textures", {})) == set(re.findall(r'"(Art/[^"]+)"\s*:', tool_text)) and manifest.get("textures"),
      "the manifest lists exactly the textures make_dark_pack.py bakes")

dp_rules = root / "src/core/rules/DarkPackSwitch.h"
check(dp_rules.is_file() and "darkpackswitch::Switch(" in dp, "DarkPack.cpp uses the tested switch (src/core/rules/DarkPackSwitch.h)")
check("MR-PAK-PROBE" not in plugin and "PakMountProbe" not in plugin, "the pak mount probe (TEMP-DIAG, recorded in the KB) is removed")

files_tool = root / "tools/plugin_files.py"
entries = {}
if files_tool.is_file():
    spec = importlib.util.spec_from_file_location("plugin_files", files_tool)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    entries = module.entries(root)
prefix = "OblivionRemastered/Binaries/Win64/OBSE/Plugins/"
for ext in ("pak", "utoc", "ucas"):
    check(entries.get(prefix + f"MenusRevamped/DarkPack/MRDark_P.{ext}") == pack / f"MRDark_P.{ext}",
          f"the release zip ships MRDark_P.{ext} in Plugins/MenusRevamped/DarkPack (tools/plugin_files.py)")
check(not any("~mods" in name for name in entries), "the zip puts nothing into ~mods itself (the plugin copies the pack)")
check(not any(name.endswith(".json") for name in entries), "the build manifest stays out of the zip")
check(prefix + "MenusRevamped.dll" in entries and prefix + "MenusRevamped.ini" in entries
      and any(n.endswith(".png") for n in entries), "the zip list still holds the DLL, the INI and the PNGs")

readme = (root / "README.md").read_text(encoding="utf-8")
check("MRDark_P" in readme and "~mods" in readme and "uninstall" in readme.lower(),
      "README tells Vortex users to switch to Classic before uninstalling, or to delete ~mods/MRDark_P.* by hand")
ini = (root / "config/MenusRevamped.defaults.ini").read_text(encoding="utf-8")
check("MRDark_P" in ini.split("uPreset", 1)[0][-600:], "the INI preset comment says Dark installs the MRDark_P pack and needs a restart")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
