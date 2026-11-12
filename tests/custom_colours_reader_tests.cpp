// Custom pack builder Phase 3 (owner 2026-11-11): the plugin reads Plugins/MenusRevamped/CustomColours.ini with the same keys,
// ranges and rules as tools/custom_colours.py (missing file/key = the Dark default, unknown key ignored with one note, broken
// value = that key's default plus one warning), plus a [Skin] section for the run-time tints (owner decision 5). The values must
// reach the run-time skin (uistyle::SkinFor(Dark)), not only darkstyle::Get(): kDarkSkin was a constexpr copy of the defaults.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include "../src/core/UiTheme.h"
#if __has_include("../src/core/rules/CustomColours.h")
#include "../src/core/rules/CustomColours.h"
#else
// Pre-fix behaviour: the plugin has no colour file reader; the compiled-in defaults are always used.
#include <vector>
namespace customcolours {
struct Note { bool warning; std::string text; };
struct Parsed { darkstyle::Style style; std::vector<Note> notes; };
inline Parsed Parse(std::string_view) { return { darkstyle::kDefaults, {} }; }
inline std::string Render(const darkstyle::Style&) { return {}; }
}
namespace darkstyle { inline void Set(const Style&) {} }
#endif
namespace fs = std::filesystem;
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", why); } }
bool Same(const darkstyle::Rgb& a, float r, float g, float b) { return a.r == r && a.g == g && a.b == b; }
unsigned Warnings(const customcolours::Parsed& p) { unsigned n = 0; for (auto& x : p.notes) n += x.warning; return n; }
std::string ReadFile(const fs::path& f) { std::ifstream in(f, std::ios::binary); std::string s{ std::istreambuf_iterator<char>(in), {} }; std::erase(s, '\r'); return s; }   // a CRLF checkout (core.autocrlf) is the same file

int main()
{
	using customcolours::Parse;
	const auto& d = darkstyle::kDefaults;

	auto p = Parse("");
	Check(Same(p.style.panel, d.panel.r, d.panel.g, d.panel.b) && p.notes.empty(), "an empty (or missing) file gives the Dark defaults, no notes");

	p = Parse("[Colours]\npanel = 10, 20, 30\nframe =  1 ,2,  3   ; comment\n[Tab]\nglyph = 200\n");
	Check(Same(p.style.panel, 10, 20, 30), "a [Colours] entry is read (r, g, b)");
	Check(Same(p.style.frame, 1, 2, 3), "spaces and a trailing ; comment are tolerated");
	Check(p.style.tab.glyph == 200 && p.style.tab.glyphDim == d.tab.glyphDim, "a [Tab] grey is read; the others keep their default");
	Check(Same(p.style.rowText, d.rowText.r, d.rowText.g, d.rowText.b), "a missing key keeps the Dark default");
	Check(p.notes.empty(), "a clean file gives no notes");

	p = Parse("\xEF\xBB\xBF[Colours]\r\ndetailsPage = 0.5\r\n");
	Check(p.style.detailsPage == 0.5f && p.notes.empty(), "a UTF-8 BOM and CRLF line ends are read (detailsPage is one number)");

	p = Parse("[Skin]\nrule = 0.1, 0.2, 0.3, 0.4\nscrollTrack = 0.5\n");
	Check(p.style.skin.rule == std::array<float, 4>{ 0.1f, 0.2f, 0.3f, 0.4f }, "[Skin] rule takes four linear values (owner decision 5)");
	Check(p.style.skin.scrollTrack == 0.5f && p.style.skin.scrollBorder == d.skin.scrollBorder, "[Skin] scrollTrack is one number");
	Check(p.style.skin.pattern == d.skin.pattern && Same(p.style.pattern, d.pattern.r, d.pattern.g, d.pattern.b),
		"[Skin] pattern and [Colours] pattern are different entries");

	p = Parse("[Colours]\nnotAColour = 1, 2, 3\n[Nope]\nx = 1\n");
	Check(p.notes.size() == 2 && Warnings(p) == 0, "an unknown key or section is ignored with one (non-warning) note each");

	// Step B (owner 2026-11-12): the builder's own [Roles] and [Sizes] sections are skipped without a note or a value change
	p = Parse("[Colours]\npanel = 10, 20, 30\n[Roles]\nbodyText = 200, 200, 200\n[Sizes]\ninventoryRowHeight = 40\n"
		"font.InventoryMainPart.inv_armor = 14\n[Skin]\nscrollTrack = 0.5\n");
	Check(p.notes.empty() && Same(p.style.panel, 10, 20, 30) && p.style.skin.scrollTrack == 0.5f &&
			Same(p.style.rowText, d.rowText.r, d.rowText.g, d.rowText.b),
		"the builder's [Roles] and [Sizes] sections are skipped silently; the sections after them are still read");

	const char* broken[]{ "[Colours]\npanel = 1, 2\n", "[Colours]\npanel = 300, 0, 0\n", "[Colours]\npanel = red\n",
		"[Colours]\npanel = 1, 2, 3, 4\n", "[Tab]\nglyph = -4\n", "[Colours]\ndetailsPage = 2\n", "[Skin]\nrule = 0.5, 0.5, 0.5\n",
		"[Skin]\nband = 1.5, 0, 0, 1\n", "[Colours]\npanel = \n" };
	for (const char* text : broken) {
		p = Parse(text);
		Check(Warnings(p) == 1 && p.notes.size() == 1, text);
		Check(Same(p.style.panel, d.panel.r, d.panel.g, d.panel.b) && p.style.tab.glyph == d.tab.glyph && p.style.detailsPage == d.detailsPage
			&& p.style.skin.rule == d.skin.rule && p.style.skin.band == d.skin.band, "a broken value keeps that key's default");
	}
	p = Parse("[Colours]\npanel = 300, 0, 0\nframe = 5, 5, 5\n");
	Check(Warnings(p) == 1 && Same(p.style.frame, 5, 5, 5), "one broken key does not stop the others");
	p = Parse("[Colours]\npanel = 300, 0, 0\n");
	Check(!p.notes.empty() && p.notes[0].text.find("panel") != std::string::npos, "the warning names the key");

	// the default file the plugin writes when it is missing (owner decision 8) is the generated config/CustomColours.ini
	const auto ini = fs::path(__FILE__).parent_path().parent_path() / "config" / "CustomColours.ini";
	const auto rendered = customcolours::Render(d);
	Check(!rendered.empty() && rendered == ReadFile(ini), "Render(defaults) is config/CustomColours.ini byte for byte (tools/custom_colours.py)");
	p = Parse(rendered);
	Check(p.notes.empty() && Same(p.style.columnLine, d.columnLine.r, d.columnLine.g, d.columnLine.b) && p.style.skin.buttonRule == d.skin.buttonRule
		&& p.style.tab.outline == d.tab.outline, "the rendered default file reads back to the defaults with no notes");
	Check(rendered.find("[Skin]") != std::string::npos, "the default file has a [Skin] section");

	// an INI colour reaches the run-time skin (kDarkSkin is no longer a compiled-in copy of the defaults)
	p = Parse("[Colours]\nrowText = 255, 0, 0\ndetailsPage = 0.2\n[Skin]\nrule = 0.1, 0.2, 0.3, 0.4\nscrollInside = 0.3\n");
	darkstyle::Set(p.style);
	const auto& dark = uistyle::SkinFor(uistyle::Preset::Dark);
	Check(darkstyle::Get().rowText.r == 255 && darkstyle::Get().rowText.g == 0, "darkstyle::Get() returns the file's values after Set()");
	Check(dark.rule == std::array<float, 4>{ 0.1f, 0.2f, 0.3f, 0.4f }, "a [Skin] tint from the file reaches the run-time Dark skin");
	Check(dark.scrollInside == 0.3f && dark.pageGray == 0.2f, "file numbers reach the skin (scrollbar, details page)");
	Check(dark.ink[0] == 1.0f && dark.ink[1] == 0.0f && dark.perkInk[0] == 1.0f, "rowText from the file reaches the run-time ink pass");
	Check(uistyle::SkinFor(uistyle::Preset::Classic).recolor == false, "Classic stays the stock skin");
	darkstyle::Set(d);
	Check(uistyle::SkinFor(uistyle::Preset::Dark).rule == d.skin.rule, "setting the defaults again restores the default skin");

	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
