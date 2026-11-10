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
    if isinstance(value, list):
        continue   # Rgb values: checked in the phase 2 block
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

# Phase 2 (owner 2026-11-10): the Inventory art of the local, never-shipped MRSkinPlayerPanel_P / MRSkinHighlight_P packs is
# baked into MRDark_P by recipes that read DarkStyle.h, so Classic is stock once those packs leave ~mods.
INVENTORY = {
    "Art/UI/Common/Backgrounds/T_UI_PlayerMenu_Background", "Art/UI/Common/T_Line_V", "Art/UI/Common/T_List_highlight_V3",
    "Art/UI/Common/Scrollbar/NewTextures/T_UI_Scrollbar_VertBtm_D", "Art/UI/Common/Scrollbar/NewTextures/T_UI_Scrollbar_VertMiddleNew_D",
    "Art/UI/Common/Scrollbar/NewTextures/T_UI_Scrollbar_VertTop_D", "Art/UI/Icons/Dynamic_Icons/menus/Icons/T_Icon_enchanted",
    "Art/UI/Modern/GameMenuLayer/Inventory/T_InventoryPreview_Background_D", "Art/UI/Modern/GameMenuLayer/Inventory/T_Inventory_pattern",
    "Art/UI/Modern/GameMenuLayer/Inventory/T_inv_details_preview_frame_D",
    "Art/UI/Modern/GameMenuLayer/Inventory/T_inv_subtabs_button_BG", "Art/UI/Modern/GameMenuLayer/Inventory/T_inv_subtabs_button_BG2",
    *(f"Art/UI/Icons/Icon_Stats/T_Property_1_{n}" for n in ("Attack", "defense", "Health", "MoneyV2", "sorting", "Weight")),
    *(f"Art/UI/Modern/GameMenuLayer/Inventory/T_inv_subtabs_button_{n}_icon" for n in ("enchantment", "overview", "skill")),
    *(f"Art/UI/Modern/GameMenuLayer/Inventory/Tabs/T_Tab_{n}" for n in ("alchemy", "all", "armor", "misc", "weapons")),
}
baked = set(manifest.get("textures", {}))
check(not (INVENTORY - set(re.findall(r'"(Art/[^"]+)"\s*:', tool_text))),
      f"make_dark_pack.py bakes all 26 Inventory textures of the local packs (missing: {sorted(INVENTORY - set(re.findall(r'\"(Art/[^\"]+)\"', tool_text)))[:4]})")
check(INVENTORY <= baked, "the shipped pack holds the Inventory textures (rebuilt with make_dark_pack.py)")
for name, value in manifest.get("style", {}).items():
    if isinstance(value, list):
        found = re.search(r"Rgb\s+" + re.escape(name) + r"\{\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*\}", style_text)
        check(found is not None and [float(x) for x in found.groups()] == value,
              f"the pack was baked from the current DarkStyle.h {name} (rebuild the pack after a colour change)")
for name in ("listHighlight", "subTabDisc", "subTabCircle", "subTabIcon", "pattern", "enchantedDot", "previewFrameLight", "previewFrameDark"):
    check(re.search(r"Rgb\s+" + name + r"\{", style_text) is not None, f"DarkStyle.h names the baked colour {name}")
# recipes on a synthetic BC3 texture: colour blocks rewritten, the stock alpha blocks kept byte for byte
spec = importlib.util.spec_from_file_location("make_dark_pack", tool)
mdp = importlib.util.module_from_spec(spec)
try:
    spec.loader.exec_module(mdp)
except Exception as error:   # noqa: BLE001
    mdp = None
    check(False, f"make_dark_pack.py imports ({error})")
if mdp is not None:
    header = bytearray(148)
    header[0:4] = b"DDS "
    header[12:16] = (8).to_bytes(4, "little")
    header[16:20] = (8).to_bytes(4, "little")
    header[84:88] = b"DX10"
    header[128:132] = (77).to_bytes(4, "little")
    blocks = bytes(range(16)) * 4
    flat = getattr(mdp, "flat_blocks", None)
    curve = getattr(mdp, "endpoint_curve", None)
    check(flat is not None and curve is not None, "make_dark_pack.py has the flat-colour and endpoint-curve recipes")
    if flat and curve:
        out = flat(bytes(header) + blocks, (247, 247, 247))[148:]
        check(all(out[i:i + 8] == blocks[i:i + 8] for i in range(0, 64, 16)), "a flat recipe keeps every alpha block")
        check(all(out[i + 8:i + 12] == bytes([0xBE, 0xF7, 0xBE, 0xF7]) and out[i + 12:i + 16] == bytes(4) for i in range(0, 64, 16)),
              "a flat recipe writes one RGB565 colour (247 -> 0xF7BE) with index 0")
        out = curve(bytes(header) + blocks, lambda v: 16)[148:]
        check(all(out[i:i + 8] == blocks[i:i + 8] and out[i + 12:i + 16] == blocks[i + 12:i + 16] for i in range(0, 64, 16)),
              "the endpoint curve keeps the alpha blocks and the colour indices")
    check(getattr(mdp, "panel_gray", lambda v: None)(0) == 16 and getattr(mdp, "panel_gray", lambda v: None)(255) == 41
          and getattr(mdp, "panel_gray", lambda v: None)(79) == 16 + round(25 * 37 / 74),
          "the panel curve is the installed one: stock brightness <= 42 -> frame, >= 116 -> panel, linear between")

# run-time art the pack replaces is gone
theme = (root / "src/core/UiTheme.h").read_text(encoding="utf-8")
check('"T_Line_V"' not in theme and not (root / "assets/Skin/ColumnLine.png").exists(),
      "the column line comes from the pack (no T_Line_V swap, no ColumnLine.png)")
sort_rules = (root / "src/sorting/rules/ItemSort.h").read_text(encoding="utf-8")
for png in ("Sort-Name", "Sort-Health", "Sort-Damage", "Sort-Armor", "Sort-Weight", "Sort-Value"):
    check(png not in sort_rules and not (root / f"assets/Sorting/DarkSort/{png}.png").exists(),
          f"{png}: the baked T_Property_1 texture is drawn (no PNG swap)")
for png in ("Sort-Cost", "Sort-Effect", "Sort-Type", "Sort-School", "Sort-Magnitude"):
    check(png in sort_rules and (root / f"assets/Sorting/DarkSort/{png}.png").is_file(), f"{png} stays a PNG (Magic art, phase 3)")
src_text = "".join(f.read_text(encoding="utf-8-sig") for f in (root / "src").rglob("*.cpp"))
# RMI-085: the picker icons are the mod's own PNGs (not game textures), so the pack cannot replace them: Dark draws the
# white copies in assets/Sorting/DarkInventory (tools/make_dark_picker_icons.py, DarkStyle.h pickerIcon), one per stock icon
widgets = (root / "src/sorting/PickerWidgets.cpp").read_text(encoding="utf-8-sig")
brush = widgets.split("void Brush(", 1)[1].split("bool PngBrush(", 1)[0] if "void Brush(" in widgets else ""
check("darkArt" in brush and 'L"DarkInventory/"' in brush, "RMI-085: Dark picker icons load the white DarkInventory/ PNG first")
stock_icons = sorted(p.name for p in (root / "assets/Sorting").glob("*.png"))
dark_icons = sorted(p.name for p in (root / "assets/Sorting/DarkInventory").glob("*.png"))
check(stock_icons and dark_icons == stock_icons,
      f"RMI-085: a white DarkInventory PNG ships for every picker icon ({len(dark_icons)} of {len(stock_icons)})")
check("Rgb pickerIcon{" in (root / "src/core/DarkStyle.h").read_text(encoding="utf-8")
      and (root / "tools/make_dark_picker_icons.py").is_file(),
      "RMI-085: the white picker icons are made from DarkStyle.h pickerIcon by tools/make_dark_picker_icons.py")
# hard coding: the Dark skin's colours live in DarkStyle.h
dark_skin = theme.split("inline constexpr Skin kDarkSkin{", 1)[1].split("};", 1)[0] if "inline constexpr Skin kDarkSkin{" in theme else ""
code = re.sub(r"//[^\n]*", "", dark_skin)
literals = re.findall(r"(?<![\w.])\d*\.\d+f?", code)
check(dark_skin and not literals, f"kDarkSkin holds no literal colours, they are named in DarkStyle.h (found {literals[:6]})")

for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
