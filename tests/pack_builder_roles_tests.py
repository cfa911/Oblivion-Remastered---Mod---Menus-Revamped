"""Custom pack builder Step B (owner 2026-11-12, decisions 38-45): roles + Advanced, tabs Colours/Text/Rows/Runtime, [Sizes],
Vanilla colours button. Tested on the model and controller (no Tk).
- decision 38: four tabs; names say what and where ("(all menus)" for a value used everywhere); Runtime = rule, guide only.
- decision 39/43: a role feeds its elements; a role change shifts each element by the same per-channel amount (its tint
  kept); an element set by hand is an "own value" until reset; an old file's per-element values load as own values.
- decision 42: text roles are a scale (100 = today); element font sizes follow unless own.
- decisions 29/33/45: [Sizes] rows per list, defaults = the compact-row values (tools/menus/layout.json).
- decisions 40, 47: Vanilla colours writes uPreset=0 (the colour pack stays in place); a good Build writes uPreset=2 back.
- the retired keys stay read and written but are not shown; [Colours]/[Skin] stay written in full (the plugin reads them)."""
import re
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
import custom_colours as cc  # noqa: E402
from pack_builder import model  # noqa: E402

ROLES = getattr(cc, "ROLES", None)
check(isinstance(ROLES, dict) and ROLES, "custom_colours.ROLES exists")
ROLES = ROLES or {}
RETIRED = {"pickerIcon"} | {f"Skin.{n}" for n in cc.RETIRED_SKIN}
RUNTIME = {"Skin.rule", "Skin.guide"}

# ---- the role table (decision 39) ----
fed = {}
for key, role in ROLES.items():
    check(role.get("label") and "(" in role["label"], f"role {key} is named by what and where ({role.get('label')!r})")
    for member in role.get("members", ()):
        fed.setdefault(member, []).append(key)
expected = ({n for n in list(cc.COLOURS) + list(cc.NUMBERS)} | {f"Skin.{n}" for n in cc.SKIN}) - RETIRED - RUNTIME
check(set(fed) == expected, f"every shown element is fed by a role (missing {sorted(expected - set(fed))}, extra {sorted(set(fed) - expected)})")
check(all(len(v) == 1 for v in fed.values()), f"each element has one role ({[k for k, v in fed.items() if len(v) > 1]})")
body = [k for k, r in ROLES.items() if "rowText" in r.get("members", ())]
check(body and set(ROLES[body[0]]["members"]) == {"rowText", "nameBox"} and "all menus" in ROLES[body[0]]["label"].lower(),
      "plain text is one option: Body text (all menus) = rowText + nameBox")
paper = [k for k, r in ROLES.items() if "parchmentDark" in r.get("members", ()) or "parchmentLight" in r.get("members", ())]
check(len(paper) == 2 and "parchmentDark" in ROLES[paper[0]]["members"] and "parchmentLight" in ROLES[paper[1]]["members"],
      f"the parchments are one shadow + highlight pair of roles ({paper})")
check(all(m in fed for m in ("altarPaper", "enchantingTabPaper", "detailsPage")) and
      all(fed[m][0] in paper for m in ("altarPaper", "enchantingTabPaper", "detailsPage")),
      "the single-value page papers are fed by the page paper pair")
scroll = [k for k, r in ROLES.items() if "Skin.scrollTrack" in r.get("members", ())]
check(scroll and "all menus but settings" in ROLES[scroll[0]]["label"].lower(), "Scrollbars (all menus but Settings)")

with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    m = model.ColourModel(temp / "none.ini")
    tabs = [label for label, _ in m.pages()]
    check(tabs == ["Colours", "Text", "Rows", "Runtime"], f"tabs Colours, Text, Rows, Runtime (decision 38) ({tabs})")
    pages = dict(m.pages())
    runtime = [e.key for _, entries in pages.get("Runtime", []) for e in entries] if pages.get("Runtime") else []
    check(sorted(runtime) == sorted(RUNTIME), f"Runtime holds only our own widgets ({runtime})")
    shown = [e.key for _, entries in pages.get("Colours", []) for e in entries]
    check(not (set(shown) & RETIRED) and not any(k.startswith("Tab.") for k in shown), "retired keys are not shown")

    # ---- role propagation keeps each tint (decision 43) ----
    rk = paper[0] if paper else None
    if rk and hasattr(m, "set_role"):
        before = {k: list(m.value("Colours", k)) for k in ("parchmentDark", "altarPaper")}
        base = list(m.role_value(rk))
        m.set_role(rk, [base[0] + 10, base[1] + 10, base[2] + 10])
        check(m.value("Colours", "parchmentDark") == [c + 10 for c in before["parchmentDark"]], "the role's first element moves with the role")
        check(m.value("Colours", "altarPaper") == [c + 10 for c in before["altarPaper"]], "altarPaper moves by the same amount, its tint kept")
        check(abs(m.value("Colours", "detailsPage") - cc.NUMBERS["detailsPage"]) > 1e-4, "the details page brightness follows the paper role")
        m.set_role(rk, [250, 250, 250])
        check(all(0 <= c <= 255 for c in m.value("Colours", "altarPaper")), "a shift is clamped to 0-255")
        m.set_role(rk, base)
        check(m.value("Colours", "altarPaper") == before["altarPaper"], "back to the role default = the Dark values again")
    else:
        check(False, "ColourModel.set_role/role_value exist")

    # ---- own values (decision 39) ----
    if body and hasattr(m, "is_own"):
        m.set_text("Colours", "nameBox", "1, 2, 3")
        check(m.is_own("nameBox") and not m.is_own("rowText"), "an element set by hand is an own value")
        m.set_role(body[0], [200, 200, 200])
        check(m.value("Colours", "nameBox") == [1.0, 2.0, 3.0] and m.value("Colours", "rowText") == [200.0, 200.0, 200.0],
              "a role change leaves an own value alone and moves the others")
        m.reset_element("nameBox")
        check(not m.is_own("nameBox") and m.value("Colours", "nameBox") == [200.0, 200.0, 200.0], "reset puts the element back on its role")
        path = m.save(temp / "roles.ini")
        text = path.read_text(encoding="utf-8")
        check("[Roles]" in text and "[Sizes]" in text and "[Colours]" in text and "[Skin]" in text and "[Tab]" in text,
              "the saved file holds [Roles] and [Sizes] and still every [Colours]/[Tab]/[Skin] value")
        again = model.ColourModel(path)
        check(again.value("Colours", "rowText") == [200.0, 200.0, 200.0] and not again.is_own("rowText") and
              list(again.role_value(body[0])) == [200.0, 200.0, 200.0], "a saved role reads back; its elements are not own values")
        check(cc.load(path)["rowText"] == [200.0, 200.0, 200.0], "the recipes read the role's value through [Colours]")
    else:
        check(False, "ColourModel.is_own/reset_element exist")

    # ---- an existing CustomColours.ini keeps working (compatibility) ----
    old = temp / "old.ini"
    old.write_text("[Colours]\nrowText = 10, 20, 30\naltarPaper = 24, 25, 28\n[Skin]\nring = 0.5, 0.5, 0.5, 1\n", encoding="utf-8")
    o = model.ColourModel(old)
    if hasattr(o, "is_own"):
        check(o.is_own("rowText") and o.value("Colours", "rowText") == [10.0, 20.0, 30.0], "an old per-element value loads as an own value")
        check(not o.is_own("altarPaper") and not o.is_own("nameBox"), "a value equal to its Dark default is not an own value")
        check(o.is_own("Skin.ring"), "an old [Skin] baked value loads as an own value")
    saved = o.save(temp / "old-saved.ini")
    check(cc.load(saved)["rowText"] == [10.0, 20.0, 30.0] and cc.load_skin(saved)["ring"] == [0.5, 0.5, 0.5, 1.0],
          "saving an old file keeps every value")

    # ---- sizes (decisions 29, 33, 42, 45) ----
    SIZES = getattr(cc, "SIZES", {})
    TEXT_ROLES = getattr(cc, "TEXT_ROLES", {})
    from pack_builder import pipeline
    check(SIZES.get("inventoryRowHeight") == 36 and SIZES.get("alchemyRowHeight") == 27 and SIZES.get("inventoryIconSize") == 32
          and SIZES.get("inventoryCountFont") == 11 and SIZES.get("skillIconSize") == 48 and SIZES.get("alchemyFilterRowHeight") == 34,
          f"[Sizes] row defaults are today's compact values ({ {k: v for k, v in SIZES.items() if 'Row' in k or 'Icon' in k or 'Count' in k} })")
    for key, role in TEXT_ROLES.items():
        check(SIZES.get(key) == 100, f"text role {key} defaults to 100 % (decision 42)")
        check(role.get("label") and role.get("members"), f"text role {key} has a label and members")
        for member in role.get("members", ()):
            fed = [f for row in pipeline.LAYOUT.values() for f in row.get("floats", []) if f.get("size") == f"font.{member}"]
            check(len(fed) == 1 and fed[0]["widget"] == member.split(".", 1)[1], f"{member} feeds its baked font size in layout.json")
    accepted = {"ItemEffect.EffectText": 22, "Stats_LevelDescription.stat_level_description": 16,
                "Stats_LevelDescription.stat_level_name": 23.5, "InventoryMainPart.inv_armor": 14, "InventoryMainPart.inv_money": 14,
                "InventoryMainPart.inv_weight": 14, "MagicMenu.Magic_SpellEffectivenes_Value": 18,
                "StatsSkillEntry.stat_skillentry_value": 16, "Stats_LevelDescription.stat_level_value": 16,
                "StatsPage3_Details.stat_detailsp3_skillbase": 18, "StatsSkillEntry.stat_skillentry_label": 22.5,
                "FilterEffectEntry.EffectText": 16}
    check(getattr(cc, "FONT_SIZES", {}) == accepted, "font size defaults = the compact values the owner accepted (2026-11-11/12)")
    check(len(TEXT_ROLES) >= 4, f"text roles body, titles, values, list row names ({list(TEXT_ROLES)})")
    s = model.ColourModel(temp / "none.ini")
    if hasattr(s, "set_size") and TEXT_ROLES:
        values = [k for k, r in TEXT_ROLES.items() if "InventoryMainPart.inv_armor" in r["members"]]
        s.set_size(values[0], 150)
        check(s.font_size("InventoryMainPart.inv_armor") == 21.0, "a 150 % values role makes 14 -> 21")
        s.set_font_size("InventoryMainPart.inv_money", 15)
        s.set_size(values[0], 100)
        check(s.font_size("InventoryMainPart.inv_money") == 15 and s.font_size("InventoryMainPart.inv_armor") == 14,
              "an element font size set by hand is an own value; the others follow the role")
        bad = False
        try:
            s.set_size("inventoryRowHeight", 0)
        except ValueError:
            bad = True
        check(bad and s.size("inventoryRowHeight") == 36, "an out-of-range size is refused, the old value kept")
        s.set_size("inventoryRowHeight", 40)
        p = s.save(temp / "sizes.ini")
        check(cc.load_sizes(p)["inventoryRowHeight"] == 40 and cc.load_sizes(p)["font.InventoryMainPart.inv_money"] == 15,
              "[Sizes] reads back through custom_colours.load_sizes")
        check(cc.load_sizes(temp / "none.ini")["inventoryRowHeight"] == 36, "a missing file = the default sizes")
        s.reset_to_dark()
        check(s.size("inventoryRowHeight") == 36 and s.font_size("InventoryMainPart.inv_money") == 14, "Reset to Dark resets sizes too")
    else:
        check(False, "ColourModel.set_size/size/font_size/set_font_size exist")
    text_tab = pages.get("Text", [])
    rows_tab = pages.get("Rows", [])
    check(text_tab and rows_tab, "the Text and Rows tabs have entries")

    # ---- Vanilla colours button (decisions 40, 47: uPreset=0; Build writes uPreset=2 back) ----
    from pack_builder import app, window
    plugins = temp / "Plugins"
    target = plugins / "MenusRevamped/CustomPack"
    target.mkdir(parents=True)
    for pack in ("MRCustom_P", "MRLayout_P"):
        for ext in ("pak", "utoc", "ucas", "json"):
            (target / f"{pack}.{ext}").write_text(pack, encoding="utf-8")
    ini = plugins / "MenusRevamped.ini"
    ini.write_text("[Log]\nuLogLevel=3\n[Style]\n; the preset\nuPreset=2\n[Layout]\nbCompactRows=1\n", encoding="utf-8")
    c = window.Controller(paks=None, target=target, retoc=None, colours_path=plugins / "MenusRevamped/CustomColours.ini")
    if hasattr(c, "vanilla"):
        message = c.vanilla()
        text = ini.read_text(encoding="utf-8")
        check("uPreset=0" in text and "uPreset=2" not in text, "Vanilla colours writes [Style] uPreset=0")
        check("; the preset" in text and "bCompactRows=1" in text and "uLogLevel=3" in text, "the rest of MenusRevamped.ini is kept")
        check(all((target / f"{p}.{e}").exists() for p in ("MRCustom_P", "MRLayout_P") for e in ("pak", "utoc", "ucas", "json")),
              "both packs stay in place (the plugin's Classic path removes the colour pack from ~mods)")
        check("start the game" in message.lower(), "the message says the game start applies it")
        saved = app.build_pack
        app.build_pack = lambda **kw: app.Result(True, "built")
        try:
            c.build(lambda line: None)
        finally:
            app.build_pack = saved
        check("uPreset=2" in ini.read_text(encoding="utf-8"), "a good Build writes uPreset=2 back (Custom)")
        app.build_pack = lambda **kw: app.Result(False, "failed")
        c.vanilla()
        try:
            c.build(lambda line: None)
        finally:
            app.build_pack = saved
        check("uPreset=0" in ini.read_text(encoding="utf-8"), "a failed Build leaves the preset alone")
        (plugins / "MenusRevamped.ini").unlink()
        c.vanilla()
        check("[Style]" in ini.read_text(encoding="utf-8") and "uPreset=0" in ini.read_text(encoding="utf-8"),
              "a missing MenusRevamped.ini gets [Style] uPreset=0")
    else:
        check(False, "Controller.vanilla exists")
    src = (root / "tools/pack_builder/window.py").read_text(encoding="utf-8")
    check("Vanilla colours" in src and "Advanced" in src, "the window has the Vanilla colours button and the Advanced switch")

print(f"{count} checks, {len(fails)} failures")
for why in fails:
    print("FAIL:", why)
sys.exit(1 if fails else 0)
