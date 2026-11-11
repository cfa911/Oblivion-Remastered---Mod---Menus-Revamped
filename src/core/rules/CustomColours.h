#pragma once
// The Custom colour file, Plugins/MenusRevamped/CustomColours.ini (tests/custom_colours_reader_tests.cpp). Same keys, ranges
// and rules as tools/custom_colours.py: [Colours] r, g, b 0-255 per element and detailsPage 0-1; [Tab] greys 0-255; [Skin] the
// run-time tints, four linear values 0-1 (or one for the scrollbar). A missing file or key = the Dark default (kDefaults); an
// unknown key or section is ignored with one note; a broken value keeps that key's default with one warning.
// Render(kDefaults) is the generated config/CustomColours.ini byte for byte: the plugin writes it when the file is missing.
#include <charconv>
#include <string>
#include <string_view>
#include <vector>
#include "core/DarkStyle.h"

namespace customcolours
{
	struct Note
	{
		bool        warning;   // false: an ignored unknown entry (info)
		std::string text;
	};

	struct Parsed
	{
		darkstyle::Style  style;
		std::vector<Note> notes;
	};

	enum class Kind { Rgb, Unit, Grey, Tint, Linear };   // 3 x 0-255, 1 x 0-1, 1 x 0-255, 4 x 0-1, 1 x 0-1

	struct Entry
	{
		const char* section;
		const char* name;
		Kind        kind;
		float* (*at)(darkstyle::Style&);
		const char* help;   // nullptr = no comment in the default file
	};

	static_assert(sizeof(darkstyle::Rgb) == 3 * sizeof(float), "Rgb is three packed floats");

#define MR_CC_RGB(n, h) Entry{ "Colours", #n, Kind::Rgb, [](darkstyle::Style& s) -> float* { return &s.n.r; }, h }
#define MR_CC_TAB(n) Entry{ "Tab", #n, Kind::Grey, [](darkstyle::Style& s) -> float* { return &s.tab.n; }, nullptr }
#define MR_CC_TINT(n, h) Entry{ "Skin", #n, Kind::Tint, [](darkstyle::Style& s) -> float* { return s.skin.n.data(); }, h }
#define MR_CC_LIN(n, h) Entry{ "Skin", #n, Kind::Linear, [](darkstyle::Style& s) -> float* { return &s.skin.n; }, h }
	// file order (tools/custom_colours.py COLOURS, NUMBERS, TAB, SKIN)
	inline const Entry kEntries[]{
		MR_CC_RGB(panel, "list page"), MR_CC_RGB(frame, "list page edge and scrollbar"), MR_CC_RGB(rowText, "row and details text"),
		MR_CC_RGB(rowSeparator, "row separator line"), MR_CC_RGB(columnLine, "row column lines"), MR_CC_RGB(rowIcon, "row icons"),
		MR_CC_RGB(headerIcon, "header sort icons"), MR_CC_RGB(pickerIcon, "sort picker icons"), MR_CC_RGB(listHighlight, "hovered row band"),
		MR_CC_RGB(subTabDisc, "selected details sub-tab disc"), MR_CC_RGB(subTabCircle, "details sub-tab circle"),
		MR_CC_RGB(subTabIcon, "details sub-tab icons"), MR_CC_RGB(pattern, "laurels beside the action button"),
		MR_CC_RGB(enchantedDot, "enchanted item dot"), MR_CC_RGB(previewFrameLight, "item preview frame, light end"),
		MR_CC_RGB(previewFrameDark, "item preview frame, dark end"), MR_CC_RGB(parchmentDark, "parchment pages, dark end"),
		MR_CC_RGB(parchmentLight, "parchment pages, light end"), MR_CC_RGB(effectBox, "Alchemy effect boxes"),
		MR_CC_RGB(nameBox, "editable name fields"), MR_CC_RGB(filterRow, "Alchemy filter rows"),
		MR_CC_RGB(filterHover, "Alchemy filter row under the cursor"), MR_CC_RGB(filterSelected, "Alchemy selected filter row"),
		MR_CC_RGB(filterMark, "Alchemy filter diamond"),
		Entry{ "Colours", "detailsPage", Kind::Unit, [](darkstyle::Style& s) -> float* { return &s.detailsPage; }, "details page darkness (0 black .. 1 stock)" },
		MR_CC_TAB(selected), MR_CC_TAB(unselected), MR_CC_TAB(gap), MR_CC_TAB(outline), MR_CC_TAB(glyph), MR_CC_TAB(glyphDim),
		MR_CC_TINT(rule, "header rules"), MR_CC_TINT(guide, "header guides"), MR_CC_TINT(ring, "skill level ring"),
		MR_CC_TINT(ornament, "stats ornament line"), MR_CC_TINT(pattern, "laurels (1 = as baked)"),
		MR_CC_TINT(band, "details effect title band"), MR_CC_TINT(buttonFill, "action button fill and arrows"),
		MR_CC_LIN(scrollTrack, "scrollbar track"), MR_CC_LIN(scrollBorder, "scrollbar border"), MR_CC_LIN(scrollInside, "scrollbar inside"),
		MR_CC_TINT(emphasisInk, "emphasis rows (perk text)"), MR_CC_TINT(buttonRule, "action button rule"),
	};
#undef MR_CC_RGB
#undef MR_CC_TAB
#undef MR_CC_TINT
#undef MR_CC_LIN

	inline int   Count(Kind k) { return k == Kind::Rgb ? 3 : k == Kind::Tint ? 4 : 1; }
	inline float Top(Kind k) { return k == Kind::Rgb || k == Kind::Grey ? 255.0f : 1.0f; }

	inline std::string_view Trim(std::string_view s)
	{
		while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
		while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
		return s;
	}

	inline bool KnownSection(std::string_view s) { return s == "Colours" || s == "Tab" || s == "Skin"; }

	inline const Entry* Find(std::string_view section, std::string_view name)
	{
		for (const auto& e : kEntries)
			if (section == e.section && name == e.name) return &e;
		return nullptr;
	}

	// The numbers of one value ("1, 2, 3"); false when one is not a number or the count or range is wrong.
	inline bool ReadValue(std::string_view raw, Kind kind, float* out)
	{
		const int want = Count(kind);
		int       got = 0;
		while (true) {
			const auto comma = raw.find(',');
			const auto part = Trim(raw.substr(0, comma));
			if (part.empty() || got == want) return false;
			float v = 0;
			const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), v);
			if (error != std::errc() || end != part.data() + part.size() || !(v >= 0.0f && v <= Top(kind))) return false;
			out[got++] = v;
			if (comma == std::string_view::npos) break;
			raw.remove_prefix(comma + 1);
		}
		return got == want;
	}

	inline Parsed Parse(std::string_view text)
	{
		Parsed out{ darkstyle::kDefaults, {} };
		if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
		std::string section;
		bool        sectionKnown = false;
		while (!text.empty()) {
			const auto newline = text.find('\n');
			auto       line = Trim(text.substr(0, newline));
			text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
			if (line.empty() || line.front() == ';' || line.front() == '#') continue;
			if (line.front() == '[') {
				const auto close = line.find(']');
				section = std::string(line.substr(1, close == std::string_view::npos ? std::string_view::npos : close - 1));
				sectionKnown = KnownSection(section);
				if (!sectionKnown) out.notes.push_back({ false, "[" + section + "] is not a known section, ignored" });
				continue;
			}
			const auto equals = line.find('=');
			if (equals == std::string_view::npos) {
				out.notes.push_back({ true, "line '" + std::string(line) + "' has no '=', ignored" });
				continue;
			}
			if (!sectionKnown && !section.empty()) continue;   // inside an unknown section: noted once at its header
			const auto  name = Trim(line.substr(0, equals));
			const auto* entry = Find(section, name);
			if (!entry) {
				out.notes.push_back({ false, "[" + section + "] " + std::string(name) + " is not a known entry, ignored" });
				continue;
			}
			auto value = line.substr(equals + 1);
			value = Trim(value.substr(0, value.find(';')));
			float numbers[4]{};
			if (!ReadValue(value, entry->kind, numbers)) {
				const int   n = Count(entry->kind);
				std::string need = n == 1 ? "one number" : (n == 3 ? "three numbers (red, green, blue)" : "four numbers (red, green, blue, alpha)");
				out.notes.push_back({ true, "[" + section + "] " + std::string(name) + " needs " + need + " from 0 to " +
					(Top(entry->kind) == 1.0f ? "1" : "255") + ", found '" + std::string(value) + "': default kept" });
				continue;
			}
			float* target = entry->at(out.style);
			for (int i = 0; i < Count(entry->kind); ++i) target[i] = numbers[i];
		}
		return out;
	}

	// a number as tools/custom_colours.py number() writes it: shortest round trip, no ".0" on whole numbers
	inline std::string Number(float v)
	{
		char buffer[32];
		const auto result = std::to_chars(buffer, buffer + sizeof(buffer), v);
		return { buffer, result.ptr };
	}

	inline std::string Render(darkstyle::Style style)
	{
		std::string out =
			"; Menus Revamped - Custom colours.\n"
			"; [Colours]: red, green, blue (0-255) per element; detailsPage is one number. [Tab]: tab strip greys (0-255).\n"
			"; [Skin]: tints drawn while the game runs: red, green, blue, alpha (0-1); the scrollbar entries are one number.\n"
			"; A missing entry uses the default Dark colour. Rebuild the Custom pack after a [Colours] or [Tab] change;\n"
			"; [Skin] changes need only a game restart.\n";
		std::string_view section;
		for (const auto& e : kEntries) {
			if (section != e.section) {
				section = e.section;
				out += "\n[" + std::string(section) + "]\n";
			}
			std::string line = std::string(e.name) + " = ";
			const float* values = e.at(style);
			for (int i = 0; i < Count(e.kind); ++i) line += (i ? ", " : "") + Number(values[i]);
			if (e.help) {
				if (line.size() < 36) line.append(36 - line.size(), ' ');
				line += "; ";
				line += e.help;
			}
			out += line + "\n";
		}
		return out;
	}
}
