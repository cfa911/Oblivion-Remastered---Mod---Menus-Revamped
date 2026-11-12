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
#define MR_CC_TAB(n, h) Entry{ "Tab", #n, Kind::Grey, [](darkstyle::Style& s) -> float* { return &s.tab.n; }, h }
#define MR_CC_TINT(n, h) Entry{ "Skin", #n, Kind::Tint, [](darkstyle::Style& s) -> float* { return s.skin.n.data(); }, h }
#define MR_CC_LIN(n, h) Entry{ "Skin", #n, Kind::Linear, [](darkstyle::Style& s) -> float* { return &s.skin.n; }, h }
	// file order (tools/custom_colours.py COLOURS, NUMBERS, TAB, SKIN)
	inline const Entry kEntries[]{
		MR_CC_RGB(panel, "Inventory and Magic list backgrounds"), MR_CC_RGB(frame, "Inventory and Magic list edges and shared scrollbar frame"), MR_CC_RGB(rowText, "Inventory and Magic list and details text"),
		MR_CC_RGB(rowSeparator, "List row divider in every list (baked)"), MR_CC_RGB(rowEquipped, "Equipped item band in every list (baked)"), MR_CC_RGB(columnLine, "Inventory and Magic list column dividers"), MR_CC_RGB(rowIcon, "Magic school, spell type and detail icons"),
		MR_CC_RGB(headerIcon, "Inventory and Magic header stat icons"), MR_CC_RGB(pickerIcon, "Inventory and Magic sort picker icons (inactive)"), MR_CC_RGB(listHighlight, "Inventory and Magic highlighted row background"),
		MR_CC_RGB(subTabDisc, "Inventory selected details tab background"), MR_CC_RGB(subTabCircle, "Inventory details tab circle"),
		MR_CC_RGB(subTabIcon, "Inventory details tab icons"), MR_CC_RGB(pattern, "Inventory action button laurels - base colour"),
		MR_CC_RGB(enchantedDot, "Inventory enchanted item dot"), MR_CC_RGB(previewFrameLight, "Inventory item preview frame highlights"),
		MR_CC_RGB(previewFrameDark, "Inventory item preview frame shadows"), MR_CC_RGB(parchmentDark, "Alchemy and other parchment menus - shadows"),
		MR_CC_RGB(parchmentLight, "Alchemy and other parchment menus - highlights"), MR_CC_RGB(effectBox, "Alchemy effect boxes"),
		MR_CC_RGB(nameBox, "Alchemy and other editable name fields"), MR_CC_RGB(filterRow, "Alchemy filter row background"),
		MR_CC_RGB(filterHover, "Alchemy hovered filter row background"), MR_CC_RGB(filterSelected, "Alchemy selected filter row background"),
		MR_CC_RGB(filterMark, "Alchemy filter row diamond"),
		MR_CC_RGB(alchemyMetal, "Alchemy frame, ingredient slots and pot - metal"), MR_CC_RGB(alchemyStone, "Alchemy frame, ingredient slots and pot - stone"),
		MR_CC_RGB(enchantingFrame, "Enchanting right panel frame - metal"), MR_CC_RGB(spellmakingFrame, "Spellmaking right panel frame - metal"), MR_CC_RGB(altarPaper, "Enchanting and Spellmaking right panel paper"), MR_CC_RGB(enchantingCrystal, "Enchanting crystal under the right panel"), MR_CC_RGB(enchantingOrnament, "Enchanting ornament under the right panel"), MR_CC_RGB(spellmakingEye, "Spellmaking eye ornament under the right panel"), MR_CC_RGB(enchantingTabPaper, "Enchanting tab strip background"), MR_CC_RGB(enchantingTabIcon, "Enchanting tab strip icons and arrows"),
		MR_CC_RGB(tabPaperDark, "Inventory and Magic category tabs - paper shadows"), MR_CC_RGB(tabPaperLight, "Inventory and Magic category tabs - paper highlights"), MR_CC_RGB(tabGlyph, "Inventory and Magic category tab icons and arrows"),
		Entry{ "Colours", "detailsPage", Kind::Unit, [](darkstyle::Style& s) -> float* { return &s.detailsPage; }, "Inventory, Magic and shared details page brightness" },
		MR_CC_TAB(selected, "Inventory and Magic selected category tab fill"), MR_CC_TAB(unselected, "Inventory and Magic unselected category tab fill"), MR_CC_TAB(gap, "Inventory and Magic gaps between category tabs"), MR_CC_TAB(outline, "Inventory and Magic category tab outlines"), MR_CC_TAB(glyph, "Inventory and Magic selected category tab icons"), MR_CC_TAB(glyphDim, "Inventory and Magic unselected category tab icons"),
		MR_CC_TINT(rule, "Inventory and Magic header horizontal lines"), MR_CC_TINT(guide, "Inventory and Magic header column guides"), MR_CC_TINT(ring, "Inventory and Magic details skill level ring"),
		MR_CC_TINT(ornament, "Inventory and Magic details decorative dividers"), MR_CC_TINT(pattern, "Inventory action button laurels - tint and opacity"),
		MR_CC_TINT(band, "Inventory and Magic details Effects heading strip (fallback)"), MR_CC_TINT(buttonFill, "Inventory and Magic details action button fill"),
		MR_CC_LIN(scrollTrack, "Scrollbar track in every menu but Settings"), MR_CC_LIN(scrollBorder, "Scrollbar thumb border in every menu but Settings"), MR_CC_LIN(scrollInside, "Scrollbar thumb fill in every menu but Settings"),
		MR_CC_TINT(emphasisInk, "Inventory and Magic details emphasized perk text"), MR_CC_TINT(buttonRule, "Inventory and Magic action button divider centres"),
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
	// the Pack Builder's own sections (Step B, owner 2026-11-12): role colours and layout sizes, baked by the builder; the plugin
	// reads neither and skips them without a note
	inline bool BuilderSection(std::string_view s) { return s == "Roles" || s == "Sizes"; }

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
				if (!sectionKnown && !BuilderSection(section))
					out.notes.push_back({ false, "[" + section + "] is not a known section, ignored" });
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
			"; [Skin]: linear red, green, blue, alpha (0-1); the three scrollbar entries are single numbers.\n"
			"; Missing entries use Dark defaults. Rebuild for baked colours, including [Skin] pattern, ornament, ring, emphasisInk and the scrollbar.\n"
			"; Other active [Skin] edits need a restart.\n"
			"; [Colours] pickerIcon and the [Tab] entries are retained with no effect in this build (tabs: tabPaperDark, tabPaperLight, tabGlyph).\n"
			"; [Skin] band, buttonFill and buttonRule are retained with no effect in this build (baked into the game's art).\n";
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
