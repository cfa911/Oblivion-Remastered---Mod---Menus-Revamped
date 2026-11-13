"""Custom pack builder Phase 7 (owner 2026-11-11, decisions 24-25): the Enchanting and Spellmaking right panels, ornaments and
the Enchanting tab strips are rebuilt from stock by colour recipes fitted to the owner's dark art (the reference; users need
no PNGs). KB: docs/reverse-engineering/dark-mode-ui.md "Enchanting menu textures" (formats, measurements).
- eight [Colours] keys with defaults from the owner art, labelled with their menu, known to the plugin and the INI;
- altar_panel:<metal> (frames: stock paper -> altarPaper, metal -> the metal colour times brightness), altar_metal:<key>
  (crystal, ornament, eye), tab_strip (stock strokes -> enchantingTabIcon, paper -> enchantingTabPaper);
- the recipes work on BC3, BC1 (alpha mode kept) and uncompressed B8G8R8A8 (T_UI_Spellmaking-Frame) with alpha unchanged;
- data rows: enchanting.json and spellmaking.json; a changed key makes the pack stale."""
import json
import struct
import sys
import tempfile
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
sys.path.insert(0, str(root / "tools"))
import custom_colours  # noqa: E402
import pack_builder.pipeline as pipe  # noqa: E402
from pack_builder import model  # noqa: E402

from PIL import Image  # noqa: E402

KEYS = {
    "enchantingFrame": [67.0, 75.0, 91.0], "spellmakingFrame": [85.0, 130.0, 182.0], "altarPaper": [24.0, 25.0, 28.0],
    "enchantingCrystal": [103.0, 159.0, 236.0], "enchantingOrnament": [72.0, 78.0, 90.0], "spellmakingEye": [86.0, 127.0, 181.0],
    "enchantingTabPaper": [27.0, 27.0, 29.0], "enchantingTabIcon": [255.0, 255.0, 255.0],
}
E, S = "Art/UI/Modern/GameMenuLayer/enchantment/", "Art/UI/Modern/GameMenuLayer/Spellmaking/"
ROWS = {
    E + "T_Enchantment_EnchantingBackground": ("enchanting", "altar_panel:enchantingFrame", ["altarPaper", "enchantingFrame"]),
    E + "T_UI_Crystal_Enchant": ("enchanting", "altar_metal:enchantingCrystal", ["enchantingCrystal"]),
    E + "T_UI_Frame_Enchant": ("enchanting", "altar_metal:enchantingOrnament", ["enchantingOrnament"]),
    # RMI-097: the three tab strips moved to tabs.json (charcoal:enchanting, no colour element); tab_strip stays tested below
    E + "Tabs/T_Enchantment_Tab_Effect": ("tabs", "charcoal:enchanting", []),
    E + "Tabs/T_Enchantment_Tab_Gear": ("tabs", "charcoal:enchanting", []),
    E + "Tabs/T_Enchantment_Tab_SoulGem": ("tabs", "charcoal:enchanting", []),
    S + "T_UI_Spellmaking-Frame": ("spellmaking", "altar_panel:spellmakingFrame", ["altarPaper", "spellmakingFrame"]),
    S + "T_UI_SpellMaking_Frame": ("spellmaking", "altar_metal:spellmakingEye", ["spellmakingEye"]),
}

# ---- 1. the keys ----
dark = custom_colours.from_darkstyle((root / "src/core/DarkStyle.h").read_text(encoding="utf-8"))
reader = (root / "src/core/rules/CustomColours.h").read_text(encoding="utf-8")
ini = (root / "config/CustomColours.ini").read_text(encoding="utf-8")
for key, value in KEYS.items():
    check(custom_colours.COLOURS.get(key) == value, f"{key} default = {value} ({custom_colours.COLOURS.get(key)})")
    check(custom_colours.HELP.get(key, "").startswith(("Enchanting", "Spellmaking")), f"{key} is labelled with its menu")
    check(dark.get(key) == value, f"DarkStyle.h holds {key}")
    check(f"MR_CC_RGB({key}," in reader, f"the plugin reader knows {key} (default file, stale check)")
check(ini == custom_colours.render(custom_colours.DEFAULTS), "config/CustomColours.ini is the rendered default file")

# ---- 2. the data rows ----
rows = {}
for path in sorted((root / "tools/menus").glob("*.json")):
    for row in json.loads(path.read_text(encoding="utf-8")).get("assets", []):
        rows[row["asset"]] = (path.stem, row)
for asset, (menu, recipe, colours) in ROWS.items():
    got_menu, row = rows.get(asset, (None, {}))
    check(got_menu == menu and row.get("recipe") == recipe, f"{Path(asset).name}: {menu}.json, {recipe} ({got_menu}, {row.get('recipe')})")
    check(sorted(row.get("colours", [])) == sorted(colours), f"{Path(asset).name}: reads {colours} ({row.get('colours')})")
    try:
        floats, rgbs, _ = pipe.recipe(recipe)
        check(sorted({*floats, *rgbs}) == sorted(colours), f"{recipe} reads exactly {colours}")
    except Exception as error:   # noqa: BLE001
        check(False, f"pipeline.recipe({recipe!r}) exists ({type(error).__name__}: {error})")

# ---- 3. the recipes on synthetic textures (BC3, BC1, B8G8R8A8) ----
V = dict(KEYS)


def paper_and_metal():
    """64x64 (a real panel is 1024): left half cream paper (bright, grey), right half gold metal with relief; a soft alpha ramp"""
    image = Image.new("RGBA", (64, 64))
    image.putdata([((225, 218, 200) if x < 32 else tuple(round(c * (0.5 + 0.015 * y)) for c in (190, 150, 70)))
                   + (60 + 3 * x,) for y in range(64) for x in range(64)])
    return image


def decoded(data, folder, name):
    (Path(folder) / name).write_bytes(data)
    return Image.open(Path(folder) / name).convert("RGBA")


def average(image, xs):
    px = [image.getpixel((x, y)) for y in range(16) for x in xs]
    return [sum(p[i] for p in px) / len(px) for i in range(4)]


with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    for fmt, kwargs in (("BC3", {"pixel_format": "DXT5"}), ("BGRA", {})):
        path = temp / f"frame_{fmt}.dds"
        paper_and_metal().save(path, **kwargs)
        stock = path.read_bytes()
        try:
            out_bytes = pipe.recipe("altar_panel:spellmakingFrame")[2](path, V, temp, None)
            check(len(out_bytes) == len(stock), f"{fmt}: size kept")
            out, ref = decoded(out_bytes, temp, f"o_{fmt}.dds"), decoded(stock, temp, f"s_{fmt}.dds")
            paper, metal = average(out.crop((4, 0, 24, 64)).resize((16, 16)), range(16)), average(out.crop((40, 0, 64, 64)).resize((16, 16)), range(16))
            check(all(abs(a - b) <= 6 for a, b in zip(paper[:3], KEYS["altarPaper"])), f"{fmt}: the paper becomes altarPaper ({[round(c) for c in paper]})")
            check(metal[2] > metal[0] + 30, f"{fmt}: the gold metal becomes the blue metal colour ({[round(c) for c in metal]})")
            check(all(out.getpixel((x, y))[3] == ref.getpixel((x, y))[3] for y in range(64) for x in range(64)), f"{fmt}: alpha unchanged")
        except Exception as error:   # noqa: BLE001
            check(False, f"{fmt}: altar_panel works ({type(error).__name__}: {error})")
    # altar_metal: colour times brightness
    path = temp / "gem.dds"
    gem = Image.new("RGBA", (16, 16))
    gem.putdata([(120 + 6 * x, 60 + 3 * x, 160, 255) for y in range(16) for x in range(16)])
    gem.save(path, pixel_format="DXT5")
    try:
        out = decoded(pipe.recipe("altar_metal:enchantingCrystal")[2](path, V, temp, None), temp, "gem_o.dds")
        left, right = average(out, range(0, 4)), average(out, range(12, 16))
        check(right[2] > right[0] + 60 and right[1] > left[1], f"altar_metal: blue, brighter where the stock is brighter ({[round(c) for c in right]})")
    except Exception as error:   # noqa: BLE001
        check(False, f"altar_metal works ({type(error).__name__}: {error})")
    # tab_strip on BC1: brown strokes -> icon colour, paper -> tab paper, transparent stays transparent
    path = temp / "strip.dds"
    strip = Image.new("RGBA", (16, 16))
    strip.putdata([(0, 0, 0, 0) if x < 4 else ((110, 60, 30, 255) if x in (6, 8) else (200, 195, 185, 255)) for y in range(16) for x in range(16)])
    strip.save(path, pixel_format="DXT1")
    try:
        out = decoded(pipe.recipe("tab_strip")[2](path, V, temp, None), temp, "strip_o.dds")
        stroke, paper = average(out, (6, 8)), average(out, range(12, 16))   # thin strokes like the real icons
        check(min(stroke[:3]) >= 200, f"tab_strip: strokes become the icon colour ({[round(c) for c in stroke]})")
        check(max(paper[:3]) <= 40, f"tab_strip: paper becomes the tab paper colour ({[round(c) for c in paper]})")
        check(all(out.getpixel((x, y))[3] == 0 for y in range(16) for x in range(4)), "tab_strip: transparent pixels stay transparent")
    except Exception as error:   # noqa: BLE001
        check(False, f"tab_strip works ({type(error).__name__}: {error})")

# ---- 3b. owner run 2026-11-11: no speckle noise in the paper, and the gems keep their stock colour ----


def grainy_panel():
    """64x64: left half cream paper with dark brown grain specks every few pixels, right half gold metal with a cyan gem block"""
    image = Image.new("RGBA", (64, 64))
    px = []
    for y in range(64):
        for x in range(64):
            if x < 32:
                px.append(((120, 95, 70) if (x * 7 + y * 13) % 11 == 0 else (225, 218, 200)) + (255,))
            elif 44 <= x < 52 and 24 <= y < 32:
                px.append((60, 190, 230, 255))   # a cyan gem in the frame
            else:
                px.append(tuple(round(c * (0.5 + 0.008 * y)) for c in (190, 150, 70)) + (255,))
    image.putdata(px)
    return image


with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    for fmt, kwargs in (("BC3", {"pixel_format": "DXT5"}), ("BGRA", {})):
        path = temp / f"grain_{fmt}.dds"
        grainy_panel().save(path, **kwargs)
        try:
            vivid = dict(V, spellmakingFrame=[200.0, 30.0, 220.0])
            out = decoded(pipe.recipe("altar_panel:spellmakingFrame")[2](path, vivid, temp, None), temp, f"g_{fmt}.dds")
            paper = [out.getpixel((x, y)) for y in range(4, 60) for x in range(4, 26)]
            check(max(p[0] for p in paper) - min(p[0] for p in paper) <= 24 and max(p[0] for p in paper) <= 60,
                  f"{fmt}: the paper has no metal-coloured specks (red {min(p[0] for p in paper)}..{max(p[0] for p in paper)})")
            gem = average(out.crop((44, 24, 52, 32)).resize((16, 16)), range(4, 12))
            check(gem[2] > 180 and gem[1] > 150 and gem[0] < 110, f"{fmt}: the cyan gem keeps its stock colour ({[round(c) for c in gem]})")
        except Exception as error:   # noqa: BLE001
            check(False, f"{fmt}: altar_panel on a grainy panel ({type(error).__name__}: {error})")

# ---- 3d. owner run 2026-11-11: the tab strips were noisy and the icons grey: grainy warm paper stays flat, strokes white ----
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    strip = Image.new("RGBA", (64, 16))
    px = []
    for y in range(16):
        for x in range(64):
            if 24 <= x < 40 and 4 <= y < 12 and x % 4 == 0:
                px.append((105, 62, 32, 255))   # thin brown icon strokes
            else:
                k = ((x * 5 + y * 3) % 7) * 2   # paper grain (the real strip: 90 % under 5 darker than around it)
                px.append((205 - k, 192 - k, 172 - k, 255))   # warm paper grain (chroma about 33)
    strip.putdata(px)
    path = temp / "strip_grain.dds"
    strip.save(path, pixel_format="DXT1")
    try:
        out = decoded(pipe.recipe("tab_strip")[2](path, V, temp, None), temp, "strip_grain_o.dds")
        paper = [out.getpixel((x, y)) for y in range(16) for x in list(range(0, 20)) + list(range(44, 64))]
        icon = [out.getpixel((x, y)) for y in range(5, 11) for x in range(28, 37, 4)]
        check(max(max(p[:3]) for p in paper) <= 45, f"tab_strip: grainy paper stays the flat paper colour (max {max(max(p[:3]) for p in paper)})")
        check(sum(min(p[:3]) for p in icon) / len(icon) >= 170, f"tab_strip: icon strokes are light, not grey (avg {sum(min(p[:3]) for p in icon) / len(icon):.0f})")
    except Exception as error:   # noqa: BLE001
        check(False, f"tab_strip on grainy paper ({type(error).__name__}: {error})")

# ---- 3e. the three stock variants share a mask built from their brighter panels ----
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    effect = Image.new("RGBA", (1024, 128), (170, 164, 154, 255))
    effect.paste((90, 82, 75, 255), (0, 96, 680, 109))
    effect.paste((159, 153, 142, 255), (186, 30, 194, 65))  # faint unselected stroke
    gear = Image.new("RGBA", (1024, 128), (208, 199, 188, 255))
    gear.paste((133, 80, 36, 255), (186, 30, 194, 65))
    gear.paste((133, 80, 36, 0), (0, 0, 8, 8))
    soul = Image.new("RGBA", (1024, 128), (200, 191, 181, 255))
    soul.paste((100, 67, 30, 255), (505, 29, 512, 65))
    stock = {}
    for suffix, strip, fmt in (("Effect", effect, "DXT1"), ("Gear", gear, "DXT5"), ("SoulGem", soul, "DXT1")):
        name = "T_Enchantment_Tab_" + suffix
        stock[name] = temp / (name + ".dds")
        strip.save(stock[name], pixel_format=fmt)

    try:
        bake = pipe.recipe("tab_strip")[2]
        for suffix in ("Effect", "Gear", "SoulGem"):
            name = "T_Enchantment_Tab_" + suffix
            out = decoded(bake(stock[name], V, temp, stock.__getitem__), temp, suffix + "_out.dds")
            check(min(out.getpixel((189, 42))[:3]) >= 220, f"tab_strip: {suffix} picks up the Gear stroke")
            check(min(out.getpixel((508, 42))[:3]) >= 220, f"tab_strip: {suffix} picks up the SoulGem stroke")
            check(max(max(out.getpixel((250, y))[:3]) for y in range(90, 115)) <= 45,
                  f"tab_strip: {suffix} paper edges stay dark")
            alpha = Image.open(stock[name]).convert("RGBA").getchannel("A")
            check(out.getchannel("A").tobytes() == alpha.tobytes(), f"tab_strip: {suffix} keeps stock alpha")
    except Exception as error:   # noqa: BLE001
        check(False, f"tab_strip across variants ({type(error).__name__}: {error})")

# ---- 3f. owner run 2026-11-11: "very pixely on all of the parchments": a smooth stock paper stays smooth (no 4x4 steps) ----
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    paper = Image.new("RGBA", (64, 64))
    paper.putdata([(150 + x + (y % 5), 140 + x + (y % 5), 120 + x + (y % 5), 255) for y in range(64) for x in range(64)])
    path = temp / "paper.dds"
    paper.save(path, pixel_format="DXT5")
    try:
        out = decoded(pipe.recipe("parchment")[2](path, {"parchmentDark": [8.0] * 3, "parchmentLight": [33.0] * 3}, temp, None), temp, "paper_o.dds")
        rows = [[out.getpixel((x, y))[1] for x in range(64)] for y in range(64)]
        across = sum(abs(r[x] - r[x - 1]) for r in rows for x in range(4, 64, 4)) / (64 * 15)
        inside = sum(abs(r[x] - r[x - 1]) for r in rows for x in range(1, 64) if x % 4) / (64 * 48)
        check(across <= inside + 0.6, f"parchment: no steps at the 4x4 block edges (across {across:.2f}, inside {inside:.2f})")
        page = E + "Tabs/T_Enchantment_Tab_Paper"
        colours = {"parchmentDark": [8.0] * 3, "parchmentLight": [33.0] * 3}
        _, keys, bake = pipe.recipe(pipe.TEXTURES[page])
        fitted = decoded(bake(path, colours, temp, None), temp, "enchanting_page.dds")
        check(set(keys) == set(colours), "Enchanting left page uses the existing parchment controls")
        old_tone = out.getpixel((36, 32))[1]
        fitted_tone = fitted.getpixel((36, 32))[1]
        check(3 <= old_tone - fitted_tone <= 8, f"Enchanting left page has the darker reference tone ({old_tone} to {fitted_tone})")
        check(out.getchannel("A").tobytes() == fitted.getchannel("A").tobytes(), "Enchanting left page keeps stock alpha")
        bright = decoded(bake(path, dict(colours, parchmentLight=[60.0] * 3), temp, None), temp, "enchanting_bright.dds")
        check(bright.getpixel((36, 32))[1] > fitted_tone, "Enchanting left page responds to parchmentLight")
    except Exception as error:   # noqa: BLE001
        check(False, f"parchment on a smooth paper ({type(error).__name__}: {error})")

# ---- 3c. the Enchanting panel uses its paper mask (T_Enchantment_Masks, red = paper): dark stains in the paper stay paper ----
check(getattr(pipe, "PANEL_MASKS", {}).get("T_Enchantment_EnchantingBackground") == ("T_Enchantment_Masks", 0),
      "the Enchanting panel's paper comes from T_Enchantment_Masks (red channel)")
check("T_Enchantment_Masks" in pipe.needed_names(), "the build extracts the mask")
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    panel = Image.new("RGBA", (64, 64))
    panel.putdata([((110, 105, 100) if x < 16 else (225, 218, 200)) + (255,) if x < 32 else (150, 150, 155, 255) for y in range(64) for x in range(64)])
    path = temp / "T_Enchantment_EnchantingBackground.dds"
    panel.save(path, pixel_format="DXT5")
    mask = Image.new("RGBA", (32, 32))
    mask.putdata([((255, 0, 0, 255) if x < 16 else (0, 255, 0, 255)) for y in range(32) for x in range(32)])
    mask_path = temp / "T_Enchantment_Masks.dds"
    mask.save(mask_path, pixel_format="DXT5")
    try:
        out = decoded(pipe.recipe("altar_panel:enchantingFrame")[2](path, dict(V, enchantingFrame=[200.0, 30.0, 30.0]), temp,
                                                                     lambda name: mask_path), temp, "masked.dds")
        stain = [out.getpixel((x, y)) for y in range(8, 56) for x in range(4, 12)]
        metal = [out.getpixel((x, y)) for y in range(8, 56) for x in range(40, 60)]
        check(max(p[0] for p in stain) <= 50, f"a dark stain inside the mask stays paper ({max(p[0] for p in stain)})")
        check(min(p[0] for p in metal) >= 100, f"outside the mask is metal ({min(p[0] for p in metal)})")
    except Exception as error:   # noqa: BLE001
        check(False, f"the masked panel works ({type(error).__name__}: {error})")

# ---- 4. a changed key makes the pack stale ----
with tempfile.TemporaryDirectory() as temp:
    ini_path = Path(temp) / "CustomColours.ini"
    ini_path.write_text(custom_colours.render(custom_colours.DEFAULTS | {"altarPaper": [90.0, 20.0, 20.0]}), encoding="utf-8")
    m = model.ColourModel(ini_path)
    check(m.needs_build(dict(custom_colours.pack_values())) == ["altarPaper"], f"a changed altarPaper needs a Build ({m.needs_build(dict(custom_colours.pack_values()))})")

# ---- 5. RMI-087 (owner 2026-11-12): the remaining brown altar text, glyphs, rules, flourishes, tiles and banners ----
# Owners: KB docs/reverse-engineering/dark-mode-ui.md "Remaining altar colour owners" (static scan + dev census, game 1.512.105.0).
INK, GOLD = [0.0723, 0.0382, 0.0284, 1.0], [0.0999, 0.0561, 0.0395, 1.0]
SM, ES = "UI/Modern/GameMenuLayer/Spellmaking/", "UI/Modern/MenuLayer/EffectSettings/"
BP_087 = {   # package: (menu json, asset path, [(offset, stock, colour)])
    "WBP_ModernMenu_SpellMakingMenu": ("spellmaking", SM + "WBP_ModernMenu_SpellMakingMenu", [
        (55826, INK, "rowText"), (56046, GOLD, "rowText"), (56204, INK, "rowText"), (57138, INK, "panel")]),
    "WBP_ModernMenu_SpellmakingEntry": ("spellmaking", SM + "WBP_ModernMenu_SpellmakingEntry", [(4659, INK, "rowText")]),
    "WBP_ModernMenu_EffectsSettings": ("spellmaking", ES + "WBP_ModernMenu_EffectsSettings", [(27340, INK, "rowText")]),
    "WBP_LegacyMenu_EffectsSettingsEntry": ("spellmaking", "UI/Legacy/MenuLayer/EffectSettings/WBP_LegacyMenu_EffectsSettingsEntry", [
        (4497, INK, "rowText"), (4574, INK, "rowText"), (4647, INK, "rowText")]),
}
TEX_087 = {
    "Art/UI/Icons/Icon_Stats/T_Property_1_Price": ("enchanting", "flat:headerIcon"),
    "Art/UI/Icons/Icon_Stats/T_Property_1_Uses": ("enchanting", "flat:headerIcon"),
    E + "T_Enchantment_AddedEffect_Decorator_Let": ("enchanting", "flat:columnLine"),
    E + "T_Enchantment_AddedEffect_Decorator_Right": ("enchanting", "flat:columnLine"),
    E + "T_Enchantment_VerticalSeparator": ("enchanting", "flat:columnLine"),
    E + "T_Enchantment_ThinSeparatorLine": ("enchanting", "flat:columnLine"),
    E + "T_Enchantment_ItemThumbnail": ("enchanting", "tone:previewFrameLight"),
    E + "T_Enchantment_EnchantEffectBackground": ("enchanting", "parchment"),
    S + "T_SpellMaking_FilterSeparator": ("spellmaking", "flat:columnLine"),
}
menus = {p.stem: json.loads(p.read_text(encoding="utf-8")) for p in (root / "tools/menus").glob("*.json")}
asset_rows = {row["asset"]: (stem, row) for stem, data in menus.items() for row in data.get("assets", [])}
for package, (menu, asset, wanted) in BP_087.items():
    got = {r["offset"]: r for r in menus[menu].get("blueprint_colours", {}).get(package, [])}
    for offset, stock, colour in wanted:
        row = got.get(offset, {})
        check(row.get("stock") == stock and row.get("colour") == colour, f"{package} {offset}: {stock} -> {colour} ({row.get('stock')}, {row.get('colour')})")
    menu_row = asset_rows.get(asset, (None, {}))
    check(menu_row[0] == menu and menu_row[1].get("recipe") == f"bp_colour:{package}", f"{package}: an asset row in {menu}.json ({menu_row[0]}, {menu_row[1].get('recipe')})")
enchant = {r["offset"]: r for r in menus["enchanting"]["blueprint_colours"]["WBP_ModernMenu_EnchantmentMenu"]}
for offset in (75158, 75493):   # the Enchanting notification banner tiles (T_LevelUp_Frame_Light tinted with the ink)
    check(enchant.get(offset, {}).get("stock") == INK and enchant.get(offset, {}).get("colour") == "panel", f"EnchantmentMenu {offset}: banner tint -> panel ({enchant.get(offset)})")
for asset, (menu, recipe) in TEX_087.items():
    got_menu, row = asset_rows.get(asset, (None, {}))
    check(got_menu == menu and row.get("recipe") == recipe, f"{Path(asset).name}: {menu}.json {recipe} ({got_menu}, {row.get('recipe')})")
# flat on an uncompressed B8G8R8A8 texture (the three separators): every pixel the colour, alpha kept
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    line = Image.new("RGBA", (8, 4))
    line.putdata([(75, 54, 46, 40 * x) for y in range(4) for x in range(8)])
    path = temp / "line.dds"
    line.save(path)
    try:
        out_bytes = pipe.recipe("flat:columnLine")[2](path, dict(V, columnLine=[99.0, 99.0, 99.0]), temp, None)
        out, ref = decoded(out_bytes, temp, "line_o.dds"), decoded(path.read_bytes(), temp, "line_s.dds")
        check(len(out_bytes) == len(path.read_bytes()), "flat on B8G8R8A8: size kept")
        check(all(out.getpixel((x, y))[:3] == (99, 99, 99) for y in range(4) for x in range(1, 8)), f"flat on B8G8R8A8: the colour ({out.getpixel((4, 1))})")
        check(all(out.getpixel((x, y))[3] == ref.getpixel((x, y))[3] for y in range(4) for x in range(8)), "flat on B8G8R8A8: alpha unchanged")
    except Exception as error:   # noqa: BLE001
        check(False, f"flat works on B8G8R8A8 ({type(error).__name__}: {error})")
    # the build's format gate (first build skipped the three separators: "the stock texture is not BC3")
    try:
        check(pipe.format_supported("flat", path.read_bytes()), "the build accepts flat on a B8G8R8A8 stock texture")
        check(not pipe.format_supported("tone", path.read_bytes()), "the build still refuses tone on a B8G8R8A8 stock texture")
    except Exception as error:   # noqa: BLE001
        check(False, f"pipeline.format_supported exists ({type(error).__name__}: {error})")

# owner run 2026-11-12: the name field's focus band (HoverEffect, art T_GenericMenu_EditableText_Hover, tint 0.995) behind the
# white name text -> listHighlight, the hovered list row band (owner choice; shared with Alchemy, Sigil Stone, Character Creation,
# custom class)
edit = {r["offset"]: r for r in menus["alchemy"]["blueprint_colours"]["WBP_ModernPrefab_EditableText"]}
check(edit.get(13364, {}).get("stock") == [0.995, 0.995, 0.995, 1.0] and edit.get(13364, {}).get("colour") == "listHighlight",
      f"WBP_ModernPrefab_EditableText 13364: HoverEffect band -> listHighlight ({edit.get(13364)})")
edit_row = asset_rows.get("UI/Modern/Prefabs/WBP_ModernPrefab_EditableText", (None, {}))[1]
check(sorted(edit_row.get("colours", [])) == ["listHighlight", "nameBox"], f"the EditableText row reads listHighlight and nameBox ({edit_row.get('colours')})")

# owner 2026-11-12: Confirm / Cancel are BAKED (no run-time ink: "we are baking all for the Dark menu"). WBP_ModernPrefab_Button is
# shared by 27 widgets; owners from the export table (KB dark-mode-ui.md "Remaining altar colour owners").
check(not (root / "src/alchemy/EffectEditorInk.cpp").exists() and "effecteditorink" not in (root / "src/core/Runtime.cpp").read_text(encoding="utf-8"),
      "no run-time effect editor ink: the button colours are baked")
BTN_INK = [0.0704, 0.0382, 0.0284]
BTN = {2332: ([0.0723, 0.0382, 0.0284, 1.0], "rowText"), 2391: ([0.0723, 0.0382, 0.0284, 1.0], "rowText"),
       2782: (BTN_INK + [0.6235], "columnLine"), 2853: (BTN_INK + [0.6235], "columnLine"),
       3295: ([1.0, 1.0, 1.0, 0.6745], "listHighlight"), 3366: (BTN_INK + [0.698], "columnLine"), 3437: (BTN_INK + [0.698], "columnLine")}
got_btn = {r["offset"]: r for r in menus["common"].get("blueprint_colours", {}).get("WBP_ModernPrefab_Button", [])}
for offset, (stock, colour) in BTN.items():
    row = got_btn.get(offset, {})
    check(row.get("stock") == stock and row.get("colour") == colour, f"WBP_ModernPrefab_Button {offset}: {stock} -> {colour} ({row.get('stock')}, {row.get('colour')})")
btn_row = asset_rows.get("UI/Modern/Prefabs/Buttons/WBP_ModernPrefab_Button", (None, {}))
check(btn_row[0] == "common" and btn_row[1].get("recipe") == "bp_colour:WBP_ModernPrefab_Button", f"WBP_ModernPrefab_Button: common.json bp_colour ({btn_row[0]}, {btn_row[1].get('recipe')})")
fill = asset_rows.get("Art/UI/Common/Buttons/T_Button_State_Hovered_LeftFill", (None, {}))
check(fill[0] == "common" and fill[1].get("recipe") == "flat:listHighlight", f"T_Button_State_Hovered_LeftFill: common.json flat:listHighlight ({fill[0]}, {fill[1].get('recipe')})")
# bp_colour keeps the stock alpha (the hover fill is 0.6745, the rules 0.62-0.70; every earlier literal has alpha 1)
try:
    uexp = bytearray(4000)
    for offset, (stock, _) in BTN.items():
        struct.pack_into("<4f", uexp, offset, *stock)
    out = pipe.bp_colour(bytes(uexp), "WBP_ModernPrefab_Button", dict(V, rowText=[239.0] * 3, columnLine=[99.0] * 3, listHighlight=[91.0] * 3))
    fill_rgba = struct.unpack_from("<4f", out, 3295)
    check(abs(fill_rgba[3] - 0.6745) < 1e-4 and abs(fill_rgba[0] - pipe.linear(91.0)) < 1e-4, f"bp_colour keeps the stock alpha ({fill_rgba})")
    check(abs(struct.unpack_from("<4f", out, 2332)[3] - 1.0) < 1e-6, "an alpha-1 literal stays alpha 1")
except Exception as error:   # noqa: BLE001
    check(False, f"bp_colour patches WBP_ModernPrefab_Button ({type(error).__name__}: {error})")

# owner run 3 (2026-11-12): the hover arrows (T_Arrow_Empty, stock 87 66 56) and the side rules (T_Button_State_Hovered_LeftLineFade,
# B8G8R8A8, stock 74 54 46) were still brown; both are drawn only by WBP_ModernPrefab_Button
for asset, recipe in (("Art/UI/Common/T_Arrow_Empty", "flat:headerIcon"), ("Art/UI/Common/Buttons/T_Button_State_Hovered_LeftLineFade", "flat:columnLine")):
    got = asset_rows.get(asset, (None, {}))
    check(got[0] == "common" and got[1].get("recipe") == recipe, f"{Path(asset).name}: common.json {recipe} ({got[0]}, {got[1].get('recipe')})")

print(f"{count} checks, {len(fails)} failures")
for why in fails:
    print("FAIL:", why)
sys.exit(1 if fails else 0)
