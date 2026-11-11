"""Dark UI pack phase 0 (owner 2026-11-10): the baked parchment pack, switched in or out at start-up by the INI preset."""
import re
import sys
from pathlib import Path
import source_paths

fails = []
count = 0
def check(ok, why):
    global count
    count += 1
    if not ok:
        fails.append(why)

root = source_paths.ROOT
# Custom pack builder Phase 3 (owner 2026-11-11): the shipped pack is MRCustom_P in assets/CustomPack (it replaced MRDark_P)
pack = root / "assets/CustomPack"
check(all((pack / f"MRCustom_P.{ext}").is_file() for ext in ("pak", "utoc", "ucas")), "the Custom pack ships (MRCustom_P .pak/.utoc/.ucas in assets/CustomPack)")
tool = root / "tools/make_dark_pack.py"
tool_text = tool.read_text(encoding="utf-8") if tool.is_file() else ""
# the asset table lives in one data file per menu (Custom pack builder Phase 1): asset -> recipe, in build order
import json as _json
data_listed = {row["asset"]: row["recipe"]
               for menu in sorted((_json.loads(f.read_text(encoding="utf-8")) for f in (root / "tools/menus").glob("*.json")),
                                  key=lambda m: m.get("order", 99))
               for row in menu.get("assets", [])}
check("Art/UI/Modern/GameMenuLayer/SpellPurchase/T_SpellPurchase_LeftPageBG" in data_listed and "to-zen" in tool_text and "DarkStyle.h" in tool_text,
      "the pack is built by tools/make_dark_pack.py from the stock parchment and DarkStyle.h")

plugin = (root / "src/core/Plugin.cpp").read_text(encoding="utf-8")
load = plugin.split("OBSE_PLUGIN_LOAD", 1)[1] if "OBSE_PLUGIN_LOAD" in plugin else ""
check("darkpack::Apply(" in load and load.index("mrconfig::Load()") < load.index("darkpack::Apply(") < load.index("RegisterListener"),
      "the plugin switches the pack at load, after reading the INI (before the engine mounts paks: MR-PAK-PROBE)")
dp_path = root / "src/core/DarkPack.cpp"
dp = dp_path.read_text(encoding="utf-8") if dp_path.is_file() else ""
rules_path = root / "src/core/rules/DarkPackSwitch.h"
switch_text = dp + (rules_path.read_text(encoding="utf-8") if rules_path.is_file() else "")
check("~mods" in switch_text and "MRDark_P" in switch_text and "Preset::Dark" in dp and "remove" in switch_text and "copy_file" in switch_text,
      "Dark copies the pack into Content/Paks/~mods, the other presets remove it")

skin = source_paths.read("picker")
page = source_paths.function(skin, "void StyleMagicPage(")
check("PlaceMagicPage" not in skin, "the Magic details page is the game's own material on the baked parchment (phase 3: no overlay)")
details = source_paths.function(skin, "void StyleDetails(")
check("darkpack::Active()" in details, "with the pack active the Inventory details page tint is white (the parchment itself is dark)")

# Phase 1 (owner 2026-11-10): the pack pipeline and the switch made release-grade.
import hashlib
import importlib.util
import json

check("--paks" in tool_text and "to-legacy" in tool_text and ".get_dds().save(" in tool_text,
      "make_dark_pack.py extracts the stock textures from the game's Paks itself (retoc to-legacy, UE4-DDS-Tools export)")
manifest_path = pack / "MRCustom_P.json"
manifest = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.is_file() else {}
check(bool(manifest), "the pack has a build manifest (assets/CustomPack/MRCustom_P.json) written by make_dark_pack.py")
style_text = (root / "src/core/DarkStyle.h").read_text(encoding="utf-8")
for name, value in manifest.get("style", {}).items():
    if isinstance(value, list):
        continue   # Rgb values: checked in the phase 2 block
    found = re.search(r"float\s+" + name + r"\s*=\s*([\d.]+)f?", style_text)
    check(found is not None and abs(float(found.group(1)) - value) < 1e-9,
          f"the pack was baked from the current DarkStyle.h {name} (rebuild the pack after a colour change)")
check(bool(manifest.get("style")), "the manifest records the DarkStyle.h values the pack was baked from")
for ext in ("pak", "utoc", "ucas"):
    path = pack / f"MRCustom_P.{ext}"
    digest = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else ""
    check(manifest.get("files", {}).get(f"MRCustom_P.{ext}") == digest, f"MRCustom_P.{ext} is the file the manifest's build wrote")
check(set(manifest.get("textures", {})) == set(data_listed) and manifest.get("textures"),
      "the manifest lists exactly the textures make_dark_pack.py bakes")

dp_rules = root / "src/core/rules/DarkPackSwitch.h"
check(dp_rules.is_file() and "darkpackswitch::SwitchCustom(" in dp, "DarkPack.cpp uses the tested switch (src/core/rules/DarkPackSwitch.h)")
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
    check(entries.get(prefix + f"MenusRevamped/CustomPack/MRCustom_P.{ext}") == pack / f"MRCustom_P.{ext}",
          f"the release zip ships MRCustom_P.{ext} in Plugins/MenusRevamped/CustomPack (tools/plugin_files.py)")
check(not any("~mods" in name for name in entries), "the zip puts nothing into ~mods itself (the plugin copies the pack)")
check([n for n in entries if n.endswith(".json")] == [prefix + "MenusRevamped/CustomPack/MRCustom_P.json"],
      "only the pack manifest ships (Phase 3: the plugin checks its game build and colours at load)")
check(prefix + "MenusRevamped.dll" in entries and prefix + "MenusRevamped.ini" in entries
      and any(n.endswith(".png") for n in entries), "the zip list still holds the DLL, the INI and the PNGs")

readme = (root / "README.md").read_text(encoding="utf-8")
check("MRCustom_P" in readme and "~mods" in readme and "uninstall" in readme.lower(),
      "README tells Vortex users to switch to Classic before uninstalling, or to delete ~mods/MRCustom_P.* by hand")
ini = (root / "config/MenusRevamped.defaults.ini").read_text(encoding="utf-8")
check("MRCustom_P" in ini.split("uPreset", 1)[0][-700:], "the INI preset comment says Custom installs the MRCustom_P pack and needs a restart")

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
check(not (INVENTORY - set(data_listed)),
      f"make_dark_pack.py bakes all 26 Inventory textures of the local packs (missing: {sorted(INVENTORY - set(data_listed))[:4]})")
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
dark_skin = theme.split("inline Skin MakeDarkSkin(", 1)[1].split("};", 1)[0] if "inline Skin MakeDarkSkin(" in theme else ""
code = re.sub(r"//[^\n]*", "", dark_skin)
literals = re.findall(r"(?<![\w.])\d*\.\d+f?", code)
check(dark_skin and not literals, f"the Dark skin (MakeDarkSkin) holds no literal colours, they come from DarkStyle.h / CustomColours.ini (found {literals[:6]})")

# Phase 3 (owner 2026-11-10): the Magic-only art and the cast-frequency text colour baked into MRDark_P. T_MagicBackground
# (shared with Quest/Help/Stats) and the _ListSmall row icons (shared with Sigil Stone and the effect lists) stay run-time
# swaps inside Magic (owner: keep the page run-time until phase 4).
SCHOOLS = [f"Art/UI/Modern/GameMenuLayer/magic/School/T_MagicSchoolSimple_{n}" for n in
           ("Alteration", "Conjuration", "Destruction", "Illusion", "Mysticism", "Restoration")]
STATS = [f"T_MagicMenuIcon_{n}" for n in ("Cost", "Custom_Spell", "Duration", "Greater_Power", "Lesser_Power", "Magnitude",
                                          "Range", "Scroll", "Spell", "Weight")]
MAGIC_TABS = [f"Art/UI/Modern/GameMenuLayer/magic/Tabs/T_Magic_Tabs_{n}" for n in ("Self", "Touch", "Target", "All", "ActiveEffect")]
TABLE = "UI/Modern/GameMenuLayer/magic/DT_UI_MagicMenu"
listed = dict(data_listed)
check(all(listed.get(a) == "flat:rowIcon" for a in SCHOOLS), "phase 3: the six row school icons are baked flat rowIcon on the stock alpha")
check(sorted(Path(a).name for a, r in listed.items() if r == "flat:rowIcon" and "T_MagicMenuIcon_" in a) == sorted(STATS),
      "phase 3: the ten details stat/type icons are baked flat rowIcon (Magic-only textures)")
check([listed.get(a) for a in MAGIC_TABS] == [f"magic_tab:{i}" for i in range(5)], "phase 3: the five Magic tab atlases are baked (magic_tab:<i>)")
# RMI-086 (owner screenshot on 9828be38): the table's Default row baked blue left the line tan: the details blueprint sets the
# line's default colour itself (SetDefaultColorAndOpacity), which replaces the Default row. The colour is set at run time.
check(TABLE not in listed, "RMI-086: DT_UI_MagicMenu is not in the pack (its Default row never reaches the cast-frequency line)")
check(not any("_ListSmall" in a or a.endswith("/T_MagicBackground") for a in listed),
      "phase 3: the shared T_MagicBackground and _ListSmall icons stay out of the pack (other menus use them)")
check(set(listed) <= baked and set(manifest.get("textures", {})) == set(listed),
      "the shipped pack holds every asset make_dark_pack.py lists (rebuilt), the table included")
check(TABLE not in manifest.get("textures", {}), "RMI-086: the shipped pack has no DT_UI_MagicMenu")
check("InkCastFrequency" not in source_paths.read("picker") and "InkCastLines" not in source_paths.read("picker"),
      "RMI-086 deferred (owner 2026-11-10): no run-time half-fix of the cast-frequency line in phase 3")
if mdp is not None:
    import struct
    patch = getattr(mdp, "rich_row_colour", None)
    check(patch is not None, "make_dark_pack.py has the rich-text row colour patch")
    if patch:
        uexp = bytearray(200)
        uexp[69:85] = struct.pack("<4f", 0.0723, 0.0382, 0.0284, 1.0)
        out = patch(bytes(uexp), "Default", [120, 165, 235])
        check(out[:69] == bytes(uexp[:69]) and out[85:] == bytes(uexp[85:]) and len(out) == len(uexp),
              "the row patch changes only the colour's 16 bytes")
        check(all(abs(a - b) < 1e-6 for a, b in zip(struct.unpack_from("<4f", out, 69), [mdp.linear(120), mdp.linear(165), mdp.linear(235), 1.0])),
              "the row patch writes DarkStyle.h sRGB as linear floats, alpha 1")
        try:
            patch(bytes(200), "Default", [1, 2, 3])
            check(False, "the row patch refuses a .uexp whose stock colour is not where it expects (game update)")
        except SystemExit:
            check(True, "")
    opaque = getattr(mdp, "opaque_rows", None)
    check(opaque is not None, "make_dark_pack.py can force the Magic tab strip opaque (stock alpha below it)")
    if opaque:
        stock_dds = bytes(header) + bytes([7]) * 16 * 4      # 8x8: 2x2 blocks, alpha bytes 7
        colour_dds = bytes(header) + bytes([9]) * 16 * 4
        out = opaque(stock_dds, colour_dds, 3)[148:]          # rows 0-2: the first block row
        check(out[0:8] == bytes([9]) * 8 and out[16:24] == bytes([9]) * 8 and out[32:40] == bytes([7]) * 8 and out[48:56] == bytes([7]) * 8,
              "the strip's block rows take the encoded (opaque) alpha, the rows below keep the stock alpha")
        check(all(out[i + 8:i + 16] == bytes([7]) * 8 for i in range(0, 64, 16)), "opaque_rows touches alpha only")
dark_art = theme.split("inline constexpr ArtSwap kDarkArt[]{", 1)[1].split("};", 1)[0] if "kDarkArt[]{" in theme else ""
check("T_Magic_Tabs_" not in dark_art and "MagicIcon-" not in dark_art and "T_MagicSchoolSimple_" not in dark_art,
      "phase 3: the baked Magic textures leave kDarkArt (no run-time swap)")
check(dark_art.count("_ListSmall\", L\"") == 11 and '"T_MagicBackground", L"MagicPage.png"' in dark_art,
      "the shared row type icons and the Magic page stay run-time swaps")
skin_assets = root / "assets/Skin"
gone = [p.name for p in skin_assets.glob("*.png") if p.name.startswith(("MagicTab-", "MagicIcon-", "MagicRow-T_MagicSchoolSimple_"))
        or p.name == "DetailsPage.png"]
check(not gone, f"phase 3: the PNGs the pack replaces are removed ({gone[:4]})")
check(len(list(skin_assets.glob("MagicRow-T_MagicMenuIcon_*_ListSmall.png"))) == 11 and (skin_assets / "MagicPage.png").is_file(),
      "the run-time Magic PNGs stay (11 row type icons, the page)")
for png in ("Sort-Cost", "Sort-Effect", "Sort-Type", "Sort-School", "Sort-Magnitude"):
    check((root / f"assets/Sorting/DarkSort/{png}.png").is_file(), f"{png} stays (the mod's own header art)")
menu_skin = source_paths.read("picker")
for name in ("PlaceMagicPage", "SyncMagicPaper", "OnMagicMenuTick", "MagicPaper", "OnIconTileEvent", "g_magicIcons",
             "g_iconTiles", "g_iconImages", "SwapIconMaterial", "OnMagicTabsEvent", "g_magicTabs", "DetailsPage.png"):
    check(name not in menu_skin, f"phase 3: {name} is gone (the pack draws the Magic details page, icons and tabs)")
check("IsMagicPage(" in source_paths.function(menu_skin, "void StyleMagicPage("), "the Magic page is still swapped per widget (phase 4 bakes it)")
light = source_paths.function(menu_skin, "bool LightIcon(")
check("darkpack::Active()" in light, "LightIcon tints the baked icons white (their colour is baked), rowIcon only without the pack")

# Alchemy recolour (owner 2026-11-11): the four Alchemy parchments baked like the Dark details parchment (stock x detailsPage,
# stock alpha kept). Shared: T_Spellmaking_LeftPaperBackground (Spellmaking), T_UI_MenuLeft (Container, Recharge, Repair,
# Sigil Stone, Training, Help) - owner accepted. Frames and lines stay stock (owner). The game's brown Alchemy text is inked light
# at run time when the menu opens, when the Filters panel opens and when a list row is generated (no per-frame work).
ALCHEMY_PAGES = ["Art/UI/Modern/GameMenuLayer/Spellmaking/T_Spellmaking_LeftPaperBackground",
                 "Art/UI/Modern/GameMenuLayer/Alchemy/T_Alchemy_paper_bg",
                 "Art/UI/Modern/GameMenuLayer/Alchemy/T_Alchemy_Right_apparatis",
                 "Art/UI/Common/Backgrounds/T_UI_MenuLeft"]
check(set(ALCHEMY_PAGES) <= baked, "Alchemy: the shipped pack holds the four parchments (rebuilt with make_dark_pack.py)")
check(not any(Path(a).name in ("T_LevelUp_Frame_Light", "T_InventorySeparator", "T_Line_02")
              for a in listed), "Alchemy: frames, lines and the right frame stay stock (owner 2026-11-11)")
ink_tree = source_paths.function(menu_skin, "void InkTree(")
check("recolor" in ink_tree and "IsDarkInk" in ink_tree and "IsWarmInk" in ink_tree and "TextBlock" in ink_tree,
      "menuskin::InkTree inks a tree's parchment-dark text with the preset ink (Dark only; coloured text kept)")
alchemy = source_paths.read("alchemy")
check("menuskin::InkTree(" in source_paths.function(alchemy, "void DecorateIngredientRow("),
      "Alchemy: every generated ingredient row is inked")
check("menuskin::InkTree(" in source_paths.function(alchemy, "void DecorateRow("),
      "Alchemy: the Filters panel rows are inked (on each bind and in the existing row pass: hover re-darkening heals)")
tick = source_paths.function(alchemy, "void Tick(")
check("menuskin::InkTree(menu" in tick and "g_menuInked" in tick and "menuskin::InkTree(filters" in tick,
      "Alchemy: the menu tree is inked once per opening, the Filters panel each time it opens")
check("menuskin::InkTree(" not in source_paths.function(alchemy, "void Layout("), "Alchemy: no ink walk in the 100 ms layout pass")

# Alchemy round 2 (owner screenshots 2026-11-11): the multiplied parchment came out green and noisy (BC 565 rounding of a
# tan texture near black): a neutral gray tone curve that keeps the grain instead. The header's category icon is the game's own
# T_Category_All_items (no Category-All.png copy) and the lightning T_Property_1_cost: both brown, so baked to a white base
# and coloured at run time (owner: colours easy to change). Effect names white, their boxes blue, the potion name box white,
# the Filters rows: hover gray, selected very dark blue, diamond blue. Colours named in DarkStyle.h.
check(all(listed.get(a) == "parchment" for a in ALCHEMY_PAGES), "Alchemy r2: the four parchments use the gray parchment recipe")
# Round 4 (owner 2026-11-11, the Dark plan): every texture is baked in its final DarkStyle.h colour, nothing is tinted at
# run time; only what has no texture (the effect text, the Filters row's plain normal/selected boxes) stays run-time.
BAKED_R4 = {"Art/UI/Icons/Icon_Stats/T_Property_1_cost": "flat:headerIcon",
            "Art/UI/Icons/Icon_Category/T_Category_All_items": "flat:headerIcon",
            "Art/UI/Common/T_UI_EditableText_FeatherIndicator": "flat:nameBox",
            "Art/UI/Modern/GameMenuLayer/Enchantment/T_GenericMenus_EditableText_Line": "flat:nameBox",
            "Art/UI/Modern/GameMenuLayer/Alchemy/T_Alchemy_Item_Default": "flat:effectBox",
            "Art/UI/Modern/GameMenuLayer/Alchemy/Filters/T_Alchemy_Filters_Highlighted": "flat:filterHover",
            "Art/UI/Icons/Icon_Stats/T_ModernCheckmark": "tone:filterMark"}
for asset, recipe_name in BAKED_R4.items():
    check(listed.get(asset) == recipe_name and asset in baked, f"Alchemy r4: {Path(asset).name} baked {recipe_name}")
for name in ("parchmentDark", "parchmentLight", "effectBox", "filterHover", "filterSelected", "filterMark", "nameBox"):
    check(re.search(r"Rgb\s+" + name + r"\{", style_text) is not None, f"Alchemy r2: DarkStyle.h names {name}")
if mdp is not None:
    gray = getattr(mdp, "gray565", None)
    check(gray is not None, "make_dark_pack.py has gray565 (a 565 colour with equal red, green and blue)")
    if gray:
        for v in (0, 14, 20, 25, 33, 41, 255):
            e = gray(v)
            check((e >> 11) * 2 == (e >> 5) & 63 and (e >> 11) == e & 31, f"gray565({v}) is neutral (no green cast)")
    curve = getattr(mdp, "parchment_curve", None)
    check(curve is not None, "make_dark_pack.py has the parchment tone curve")
    if curve:
        dark, light = 12, 40
        check(curve(0, 100, 200, dark, light) == dark and curve(255, 100, 200, dark, light) == light
              and dark < curve(150, 100, 200, dark, light) < light,
              "the parchment curve maps the stock brightness range onto parchmentDark..parchmentLight (grain kept)")
tex_paths = source_paths.function(alchemy, "constexpr const wchar_t* kTexPaths[]") or alchemy
check("T_Category_All_items" in alchemy and "Category-All.png" not in alchemy,
      "Alchemy r2: the category sort icon is the game's T_Category_All_items, not the mod's Category-All.png copy")
# Round 4 (owner 2026-11-11: "a full native dark mode", baked like the rest): blueprint colour literals are patched in the
# pack at verified .uexp offsets (KB docs/alchemy/internals.md "Blueprint colour literals"): the Filters row boxes (row and the
# Must Contain button, both Alchemy-only), the effect names under the ingredient list, the potion name field's text colour.
BP = {"UI/Modern/GameMenuLayer/Alchemy/Filters/WBP_ModernMenu_FilterEffectEntry": "bp_colour:WBP_ModernMenu_FilterEffectEntry",
      "UI/Modern/GameMenuLayer/Common/WBP_ModernPrefab_SelectableButton": "bp_colour:WBP_ModernPrefab_SelectableButton",
      "UI/Modern/GameMenuLayer/Alchemy/WBP_ModernMenu_ItemEffect": "bp_colour:WBP_ModernMenu_ItemEffect",
      "UI/Modern/Prefabs/WBP_ModernPrefab_EditableText": "bp_colour:WBP_ModernPrefab_EditableText"}
for asset, recipe_name in BP.items():
    check(listed.get(asset) == recipe_name and asset in baked, f"Alchemy r4: {Path(asset).name} colours baked ({recipe_name})")
if mdp is not None:
    table = getattr(mdp, "BLUEPRINT_COLOURS", {})
    check(table.get("WBP_ModernMenu_FilterEffectEntry") == [(2893, (0.5395, 0.4969, 0.4452, 1.0), "filterRow"),
                                                            (3002, (0.3968, 0.3325, 0.2831, 1.0), "filterSelected")]
          and table.get("WBP_ModernPrefab_SelectableButton") == [(1734, (0.5395, 0.4969, 0.4452, 1.0), "filterRow"),
                                                               (1813, (0.3968, 0.3325, 0.2831, 1.0), "filterSelected")]
          and table.get("WBP_ModernMenu_ItemEffect") == [(1856, (0.0723, 0.0382, 0.0284, 1.0), "rowText")]
          and table.get("WBP_ModernPrefab_EditableText") == [(258, (0.0723, 0.0382, 0.0284, 1.0), "nameBox")],
          "Alchemy r4: the blueprint colour table holds the census offsets and stock values")
    patch = getattr(mdp, "bp_colour", None)
    check(patch is not None, "make_dark_pack.py has the blueprint colour patch")
    if patch:
        uexp = bytearray(3100)
        struct.pack_into("<4f", uexp, 2893, 0.5395, 0.4969, 0.4452, 1.0)
        struct.pack_into("<4f", uexp, 3002, 0.3968, 0.3325, 0.2831, 1.0)
        out = patch(bytes(uexp), "WBP_ModernMenu_FilterEffectEntry", {"filterRow": [66, 66, 66], "filterSelected": [14, 22, 42]})
        check(len(out) == len(uexp) and out[:2893] == bytes(uexp[:2893]) and out[2909:3002] == bytes(uexp[2909:3002]),
              "the blueprint patch changes only the colours' 16 bytes")
        check(abs(struct.unpack_from("<f", out, 2893)[0] - mdp.linear(66)) < 1e-6 and struct.unpack_from("<f", out, 3002 + 12)[0] == 1.0,
              "the blueprint patch writes DarkStyle.h sRGB as linear floats, alpha 1")
        try:
            patch(bytes(3100), "WBP_ModernMenu_FilterEffectEntry", {"filterRow": [1, 1, 1], "filterSelected": [1, 1, 1]})
            check(False, "the blueprint patch refuses a .uexp whose stock colour is not where it expects (game update)")
        except SystemExit:
            check(True, "")
# nothing in Alchemy is tinted at run time any more except the round 1 plain-text ink pass
check("void StyleDark(" not in alchemy and "void StyleFilterRow(" not in alchemy and "menuskin::Paint(" not in alchemy,
      "Alchemy r4: no run-time colouring of art or blueprint colours (all baked)")
check(re.search(r"Rgb\s+iconBase\{", style_text) is None, "Alchemy r4: no white-base colour (icons baked in their final colour)")
check("void Paint(" not in menu_skin and "void CensusTree(" not in menu_skin, "Alchemy r4: the run-time painter and the census (TEMP-DIAG) are gone")
if mdp is not None:
    tone = getattr(mdp, "tone_colour", None)
    check(tone is not None and tone(255, 255, (92, 140, 214)) == (92, 140, 214) and tone(0, 255, (92, 140, 214)) == (0, 0, 0)
          and tone(128, 255, (92, 140, 214))[2] < 214, "the tone recipe keeps the art's shading in one colour (the diamond's relief)")
check(re.search(r"Rgb\s+filterRow\{", style_text) is not None, "Alchemy r3: DarkStyle.h names filterRow")
search_bar = source_paths.read("search")
check("menus::Menu::kAlchemy" in search_bar.split("uistyle::SearchFor(", 1)[1].split(";", 1)[0] if "uistyle::SearchFor(" in search_bar else False,
      "Alchemy r3: the Alchemy and Filters search bars take the Dark Inventory/Magic search theme")


# Round 5 (owner screenshot 2026-11-11): the potion's effect lines (WBP_LegacyMenu_AddedEffects: name, magnitude, duration)
# and their stat icons were brown; baked white like the Magic lines. Shared: Inventory/Magic effect lists (already white in
# Dark), Sigil Stone, the Modern added-effects list (Enchanting, Spellmaking) - owner accepted.
check(listed.get("UI/Legacy/GameMenuLayer/Commun/WBP_LegacyMenu_AddedEffects") == "bp_colour:WBP_LegacyMenu_AddedEffects"
      and "UI/Legacy/GameMenuLayer/Commun/WBP_LegacyMenu_AddedEffects" in baked,
      "Alchemy r5: the effect line blueprint's brown ink is baked white")
if mdp is not None:
    check([o for o, _, n in getattr(mdp, "BLUEPRINT_COLOURS", {}).get("WBP_LegacyMenu_AddedEffects", []) if n == "rowText"]
          == [18182, 18251, 18320, 18409, 18554], "Alchemy r5: the five brown ink literals of the effect line (census offsets)")
for asset in ("Art/UI/Icons/Icon_Stats/T_Property_1_magnitude", "Art/UI/Icons/Icon_Stats/T_Property_1_time"):
    check(listed.get(asset) == "flat:headerIcon" and asset in baked, f"Alchemy r5: {Path(asset).name} baked white")


for why in fails:
    print("FAIL", why)
print(f"{count} checks, {len(fails)} failures")
sys.exit(1 if fails else 0)
