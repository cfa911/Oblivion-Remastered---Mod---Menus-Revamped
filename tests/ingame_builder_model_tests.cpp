// In-game Pack Builder Phase 2: the builder window's colour and size model in C++ (src/builder/ColourModel.h), a port of
// tools/pack_builder/model.py and tools/custom_colours.py (KB mods/menus-revamped/evidence/in-game-pack-builder-plan.md).
// Byte parity with the Python builder's saved file is checked separately by tests/ingame_builder_parity_tests.py.
// - roles feed their elements and shift them by the same per-channel amount (decisions 39, 43); a hand-set element is an
//   own value until Reset; an old file's per-element values load as own values
// - text role scales move their font sizes unless own (decision 42); sizes keep their ranges
// - the colour file is read with the Python builder's rules (a broken value refuses the whole file) and written in its layout
// - Vanilla colours writes only [Style] uPreset in MenusRevamped.ini (decision 40/47)
#include <cmath>
#include <cstdio>
#include <string>
#include "../src/builder/ColourModel.h"
#include "../src/core/rules/CustomColours.h"

unsigned checks = 0, failures = 0;
void Check(bool ok, const char* why)
{
	++checks;
	if (!ok) {
		++failures;
		std::printf("FAIL %s\n", why);
	}
}
bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }
bool Rgb(const builder::model::Value& v, double r, double g, double b) { return v.n == 3 && Near(v.v[0], r) && Near(v.v[1], g) && Near(v.v[2], b); }

int main()
{
	using namespace builder::model;
	std::string error;

	// ---- numbers as Python writes them (custom_colours.number, round) ----
	Check(Number(41.0) == "41" && Number(-0.0) == "0", "whole numbers have no .0");
	Check(Number(0.1248) == "0.1248" && Number(23.5) == "23.5" && Number(0.025) == "0.025", "shortest round trip");
	Check(Number(0.0003) == "0.0003", "Python repr keeps 0.0003 fixed (not 3e-04)");
	Check(Number(0.00005) == "5e-05", "Python repr: below 1e-4 in exponent form");
	Check(Number(0.123456789) == "0.123456789", "a typed value keeps its digits (double, not float)");
	Check(PyRound(2.5) == 2.0 && PyRound(3.5) == 4.0 && PyRound(-0.4) == 0.0, "round() is half to even");
	Check(Near(Round4(0.12345678), 0.1235) && Near(Round4(0.0782354), 0.0782), "round(x, 4)");

	// ---- the default file: the plugin's default [Colours]/[Tab]/[Skin] text, then [Roles] and [Sizes] ----
	ColourModel m;
	const auto plugin = customcolours::Render(darkstyle::kDefaults);
	const auto text = m.Render();
	Check(text.size() > plugin.size() && text.compare(0, plugin.size(), plugin) == 0, "defaults render the plugin's default file first");
	Check(text.find("\n[Roles]\n; The builder's role colours (sRGB 0-255); the elements above hold the values in use.\nbodyText = 239, 239, 239\n") != std::string::npos,
		"[Roles] section with the role defaults");
	Check(text.find("\n[Sizes]\n; Text role scales in percent; row sizes and font sizes in points. Baked into MRLayout_P by Build.\ntextBodyScale = 100\n") != std::string::npos,
		"[Sizes] section");
	Check(text.find("font.FilterEffectEntry.EffectText = 16\n") != std::string::npos && text.ends_with("= 16\n"), "sizes end with the last font size");

	// ---- tables (decision 38) ----
	Check(Roles().size() == 18 && TextRoles().size() == 4 && RowLists().size() == 10, "18 colour roles, 4 text roles, 10 row lists");
	Check(RuntimeGroups().size() == 3 && RuntimeGroups()[0].keys.size() == 2 && RuntimeGroups()[1].keys.size() == 4 && RuntimeGroups()[2].keys.size() == 5,
		"Runtime: header lines, Alchemy filters, controller keyboard");
	const char* thumb = m.RoleOf("Skin.scrollInside");
	Check(thumb && std::string(thumb) == "scrollThumb" && m.RoleOf("pickerIcon") == nullptr && m.RoleOf("Skin.rule") == nullptr,
		"role lookup; retired and runtime keys have none");

	// ---- roles keep each tint (decision 43) ----
	const auto base = m.RoleValue("paperShadow");
	Check(m.SetRole("paperShadow", { base[0] + 10, base[1] + 10, base[2] + 10 }, error), "role set");
	Check(Rgb(m.ByKey("parchmentDark").value, 18, 18, 18) && Rgb(m.ByKey("altarPaper").value, 34, 35, 38), "members shift by the role change");
	Check(!Near(m.ByKey("detailsPage").value.v[0], 0.01), "a one-number member follows (linear brightness)");
	Check(m.SetRole("paperShadow", { 250, 250, 250 }, error) && m.ByKey("altarPaper").value.v[2] <= 255, "a shift is clamped");
	m.SetRole("paperShadow", base, error);
	Check(Rgb(m.ByKey("altarPaper").value, 24, 25, 28) && Near(m.ByKey("detailsPage").value.v[0], 0.01), "back to the role default = Dark again");
	Check(!m.SetRole("paperShadow", { 300, 0, 0 }, error) && !error.empty(), "a role out of range is refused");
	Check(m.SetRoleText("bodyText", "200, 200, 200", error) && m.RoleText("bodyText") == "200, 200, 200", "role from text");
	m.SetRoleText("bodyText", "239, 239, 239", error);

	// ---- own values (decision 39) ----
	Check(m.SetText("nameBox", "1, 2, 3", error) && m.IsOwn("nameBox") && !m.IsOwn("rowText"), "a hand-set element is own");
	m.SetRole("bodyText", { 200, 200, 200 }, error);
	Check(Rgb(m.ByKey("nameBox").value, 1, 2, 3) && Rgb(m.ByKey("rowText").value, 200, 200, 200), "a role leaves own values alone");
	m.ResetElement("nameBox");
	Check(!m.IsOwn("nameBox") && Rgb(m.ByKey("nameBox").value, 200, 200, 200), "Reset puts the element back on its role");
	Check(!m.SetText("rowText", "1, 2", error) && error.find("rowText") != std::string::npos && Rgb(m.ByKey("rowText").value, 200, 200, 200),
		"a broken typed value is refused, the message names it, the old value stays");
	Check(m.Text("rowText") == "200, 200, 200" && m.Text("detailsPage") == "0.01" && m.Text("Skin.guide") == "0.55, 0.55, 0.55, 0.13", "value text");

	// ---- picker colours ----
	m.SetRgb("Skin.guide", { 255, 0, 0 });
	Check(Near(m.ByKey("Skin.guide").value.v[0], 1.0) && Near(m.ByKey("Skin.guide").value.v[3], 0.13), "a picked [Skin] colour goes in linear, alpha kept");
	const auto sw = m.Swatch("Skin.rule");
	Check(sw[0] == 99 && sw[1] == 99 && sw[2] == 99, "a linear tint shows as its sRGB swatch");
	m.SetRgb("Tab.selected", { 10, 200, 30 });
	Check(Near(m.ByKey("Tab.selected").value.v[0], PyRound(0.2126 * 10 + 0.7152 * 200 + 0.0722 * 30)), "a grey takes the picked luminance");

	// ---- a file round trip ----
	ColourModel back;
	Check(back.Load(m.Render(), error), "the saved file reads back");
	Check(back.Render() == m.Render(), "read + write is stable");
	Check(Rgb(back.ByKey("rowText").value, 200, 200, 200) && !back.IsOwn("rowText") && Near(back.RoleValue("bodyText")[0], 200),
		"a saved role reads back; its elements are not own values");

	// ---- an old file keeps working ----
	ColourModel old;
	Check(old.Load("[Colours]\nrowText = 10, 20, 30\naltarPaper = 24, 25, 28\n[Skin]\nring = 0.5, 0.5, 0.5, 1\n", error), "old file loads");
	Check(old.IsOwn("rowText") && Rgb(old.ByKey("rowText").value, 10, 20, 30), "an old per-element value loads as own");
	Check(!old.IsOwn("altarPaper") && !old.IsOwn("nameBox"), "a value equal to its default is not own");
	Check(old.Render().find("\nring = 0.5, 0.5, 0.5, 1  ") != std::string::npos, "retired keys stay written");

	// ---- reading rules (configparser + custom_colours.parse) ----
	ColourModel r;
	Check(!r.Load("[Colours]\npanel = 1, 2\n", error) && error.find("panel") != std::string::npos, "a broken value refuses the file, naming it");
	Check(!r.Load("panel = 1, 2, 3\n", error), "an entry before any section is an error");
	Check(!r.Load("[Colours]\npanel = 1, 2, 3\npanel = 4, 5, 6\n", error), "a duplicate entry is an error");
	Check(r.Load("\xEF\xBB\xBF[Colours]\npanel = 1, 2, 3 ; a comment\nmystery = 4\n[Roles]\nring = 1, 2, 3\n[Other]\nx = 1\n", error) &&
			Rgb(r.ByKey("panel").value, 1, 2, 3),
		"BOM, an inline comment, unknown and retired entries are fine");
	Check(r.Load("[Colours]\r\npanel: 5, 6, 7\r\n", error) && Rgb(r.ByKey("panel").value, 5, 6, 7), "CRLF and ':' as configparser reads them");
	Check(r.Load("", error) && r.Render() == ColourModel().Render(), "no file = the defaults");

	// ---- sizes (decisions 29, 33, 42) ----
	ColourModel s;
	Check(s.SetSize("textValueScale", "150", error) && Near(s.Size("font.InventoryMainPart.inv_armor"), 21), "150 % makes 14 -> 21");
	s.SetSize("font.InventoryMainPart.inv_money", "15", error);
	s.SetSize("textValueScale", "100", error);
	Check(Near(s.Size("font.InventoryMainPart.inv_money"), 15) && Near(s.Size("font.InventoryMainPart.inv_armor"), 14) && s.IsOwnSize("font.InventoryMainPart.inv_money"),
		"a hand-set font size is own; the others follow");
	Check(!s.SetSize("inventoryRowHeight", "0", error) && Near(s.Size("inventoryRowHeight"), 36), "out of range refused, old value kept");
	Check(!s.SetSize("textBodyScale", "201", error) && !s.SetSize("textBodyScale", "abc", error), "scale range 50-200; not a number");
	Check(s.SetSize("textTitleScale", "133", error) && Near(s.Size("font.Stats_LevelDescription.stat_level_name"), 31.5), "font sizes round to halves");
	s.ResetSize("font.InventoryMainPart.inv_money");
	Check(!s.IsOwnSize("font.InventoryMainPart.inv_money") && Near(s.Size("font.InventoryMainPart.inv_money"), 14), "Reset puts a font size back on its scale");
	Check(s.SizeText("textTitleScale") == "133", "size text");
	s.SetSize("inventoryRowHeight", "40", error);
	s.ResetToDark();
	Check(s.Render() == ColourModel().Render(), "Reset to Dark resets colours, roles and sizes");

	// ---- Vanilla colours: only [Style] uPreset changes (app.set_preset) ----
	Check(SetPreset("[Log]\nuLogLevel=3\n[Style]\n; the preset\nuPreset=2\n[Layout]\nbCompactRows=1\n", 0) ==
			"[Log]\nuLogLevel=3\n[Style]\n; the preset\nuPreset=0\n[Layout]\nbCompactRows=1\n",
		"the uPreset line is replaced, the rest kept");
	Check(SetPreset("", 0) == "[Style]\nuPreset=0\n", "a missing file gets the section");
	Check(SetPreset("\xEF\xBB\xBF[Style]\r\nbX=1\r\n[Layout]\r\n", 2) == "\xEF\xBB\xBF[Style]\r\nbX=1\r\nuPreset=2\r\n[Layout]\r\n",
		"BOM and CRLF kept; inserted at the end of [Style]");
	Check(SetPreset("[Log]\nuLogLevel=3", 0) == "[Log]\nuLogLevel=3\n[Style]\nuPreset=0\n", "a missing section is appended");

	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
