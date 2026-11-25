#pragma once
// In-game Pack Builder (Phase 2): the builder window's colour and size model, a C++ port of tools/pack_builder/model.py with
// the reading and writing rules of tools/custom_colours.py. Pure (no game, engine or file calls): tests/ingame_builder_model_tests.cpp,
// and tests/ingame_builder_parity_tests.py proves the saved text equals the Python builder's for the same edits, byte for byte.
// KB mods/menus-revamped/evidence/in-game-pack-builder-plan.md (Phase 2), custom-pack-builder-plan.md decisions 38-45.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// [Colours] r, g, b 0-255 (detailsPage one number 0-1); [Tab] greys 0-255; [Skin] linear r, g, b, a 0-1 (the scrollbar entries
// one number); [Roles] sRGB 0-255; [Sizes] text role scales (percent) and row/font sizes. Values are doubles and printed the
// way Python's repr prints them, so a typed "0.123456789" is written back unchanged. Roles feed their elements: a role change
// shifts every member by the same per-channel amount in sRGB (each element's tint kept, decision 43); an element set by hand is
// an own value until Reset; a file value that differs from what its role gives loads as an own value.
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/rules/CustomColours.h"

namespace builder::model
{
	enum class Kind { Rgb, Number, Grey, Rgba, Scalar };   // [Colours] 3 / 1 (detailsPage), [Tab] 1, [Skin] 4 / 1

	struct Value
	{
		int                   n = 0;   // 1, 3 or 4 numbers
		std::array<double, 4> v{};
	};

	struct Entry
	{
		std::string section, name, help, key;   // key: [Colours] names as they are, "Skin.<name>", "Tab.<name>"
		Kind        kind{};
		Value       def, value;
	};

	struct Role
	{
		const char*              key;
		const char*              label;
		std::vector<std::string> members;
	};

	struct TextRole
	{
		const char*              key;
		const char*              label;
		std::vector<std::string> members;   // "<blueprint>.<widget>" font sizes ([Sizes] font.<member>)
	};

	struct RowSize
	{
		const char* key;
		const char* what;
		double      def;
	};

	struct RowList
	{
		const char*          label;
		std::vector<RowSize> rows;
	};

	struct RuntimeGroup
	{
		const char*              heading;
		std::vector<std::string> keys;
	};

	// ---- the tables (custom_colours.py ROLES, TEXT_ROLES, FONT_SIZES, ROW_LISTS, RUNTIME_SKIN; same order) ----
	inline const std::vector<Role>& Roles()
	{
		static const std::vector<Role> roles{
			{ "bodyText", "Body text (all menus)", { "rowText", "nameBox" } },
			{ "emphasis", "Highlighted words (perk descriptions in the item details)", { "Skin.emphasisInk" } },
			{ "paperShadow", "Page paper - shadows (all menus with paper: details pages, Alchemy, Enchanting, Spellmaking, Container, Repair, Recharge, Training, Sigil Stone, Help)",
				{ "parchmentDark", "altarPaper", "detailsPage" } },
			{ "paperLight", "Page paper - highlights (all menus with paper)", { "parchmentLight" } },
			{ "listPanel", "List background and frame (Inventory, Magic, Container, Barter)", { "panel", "frame" } },
			{ "lines", "Lines and separators (all menus)", { "rowSeparator", "columnLine", "Skin.ornament" } },
			{ "icons", "Icons (all menus)", { "rowIcon", "headerIcon", "subTabIcon" } },
			{ "highlight", "Highlighted row and hover (all menus)", { "listHighlight", "filterHover", "subTabDisc", "subTabCircle" } },
			{ "equipped", "Equipped item band (all lists)", { "rowEquipped" } },
			{ "stolen", "Stolen item mark (Inventory, Container, Barter and Enchanting rows)", { "stolenMark" } },
			{ "accent", "Accent blue (enchanted dot, Alchemy effect boxes and selected filters)",
				{ "enchantedDot", "effectBox", "filterSelected", "filterMark", "filterOutline", "filterRow" } },
			{ "metal", "Metal frames and ornaments (Alchemy, Enchanting, Spellmaking)",
				{ "alchemyMetal", "alchemyStone", "enchantingFrame", "spellmakingFrame", "enchantingOrnament", "spellmakingEye", "enchantingCrystal" } },
			{ "preview", "Item preview frame (Inventory details)", { "previewFrameDark", "previewFrameLight" } },
			{ "laurels", "Laurels and flower ornaments (Inventory action button, popup titles)", { "pattern", "Skin.pattern" } },
			{ "scrollbars", "Scrollbars - track and thumb border (all menus but Settings)", { "Skin.scrollTrack", "Skin.scrollBorder" } },
			{ "scrollThumb", "Scrollbar thumb (all menus but Settings)", { "Skin.scrollInside" } },
			{ "accentText", "Titles, headings and highlighted values (all menu titles, Magic costs and values)", { "menuTitle", "accentValue" } },
			{ "effectGold", "Effect details (effect type, magnitude label and icons, amounts and durations)", { "effectLabel", "effectIcon", "effectValue" } },
		};
		return roles;
	}

	inline const std::vector<TextRole>& TextRoles()
	{
		static const std::vector<TextRole> roles{
			{ "textBodyScale", "Body text size (Alchemy ingredient effects, Character level description)",
				{ "ItemEffect.EffectText", "Stats_LevelDescription.stat_level_description" } },
			{ "textTitleScale", "Title size (Character level name)", { "Stats_LevelDescription.stat_level_name" } },
			{ "textValueScale", "Number size (Inventory armour, gold and weight; Magic spell effectiveness; Character skill values)",
				{ "InventoryMainPart.inv_armor", "InventoryMainPart.inv_money", "InventoryMainPart.inv_weight", "MagicMenu.Magic_SpellEffectivenes_Value",
					"StatsSkillEntry.stat_skillentry_value", "Stats_LevelDescription.stat_level_value", "StatsPage3_Details.stat_detailsp3_skillbase" } },
			{ "textRowNameScale", "List row name size (Character skills, Alchemy effect filters)",
				{ "StatsSkillEntry.stat_skillentry_label", "FilterEffectEntry.EffectText" } },
		};
		return roles;
	}

	struct FontSize
	{
		const char* member;
		double      def;
	};

	inline const std::vector<FontSize>& FontSizes()   // FONT_SIZES order: the [Sizes] file order of the font.* keys
	{
		static const std::vector<FontSize> fonts{
			{ "ItemEffect.EffectText", 22.0 }, { "Stats_LevelDescription.stat_level_description", 16.0 },
			{ "Stats_LevelDescription.stat_level_name", 23.5 },
			{ "InventoryMainPart.inv_armor", 14.0 }, { "InventoryMainPart.inv_money", 14.0 }, { "InventoryMainPart.inv_weight", 14.0 },
			{ "MagicMenu.Magic_SpellEffectivenes_Value", 18.0 }, { "StatsSkillEntry.stat_skillentry_value", 16.0 },
			{ "Stats_LevelDescription.stat_level_value", 16.0 }, { "StatsPage3_Details.stat_detailsp3_skillbase", 18.0 },
			{ "StatsSkillEntry.stat_skillentry_label", 22.5 }, { "FilterEffectEntry.EffectText", 16.0 },
		};
		return fonts;
	}

	inline const std::vector<RowList>& RowLists()
	{
		static const std::vector<RowList> lists{
			{ "Inventory, Container and Barter rows", { { "inventoryRowHeight", "Row height", 36 }, { "inventoryIconSize", "Icon size", 32 },
				{ "inventoryCountFont", "Item count font size (on the icon)", 11 } } },
			{ "Magic rows", { { "magicRowHeight", "Row height", 36 }, { "magicIconSize", "Icon size", 32 } } },
			{ "Alchemy ingredient rows", { { "alchemyRowHeight", "Row height", 27 }, { "alchemyIconSize", "Icon size", 26 } } },
			{ "Alchemy effect filter rows", { { "alchemyFilterRowHeight", "Row height", 34 } } },
			{ "Enchanting rows", { { "enchantingRowHeight", "Row height", 36 }, { "enchantingIconSize", "Icon size", 32 } } },
			{ "Spellmaking rows", { { "spellmakingRowHeight", "Row height", 36 }, { "spellmakingIconSize", "Icon size", 32 } } },
			{ "Sigil Stone rows", { { "sigilStoneRowHeight", "Row height", 36 }, { "sigilStoneIconSize", "Icon size", 32 } } },
			{ "Spell Purchase rows", { { "spellPurchaseRowHeight", "Row height", 36 }, { "spellPurchaseIconSize", "Icon size", 32 } } },
			{ "Quest rows", { { "questRowHeight", "Row height", 36 }, { "questIconSize", "Icon size", 32 } } },
			{ "Character skill rows", { { "skillRowHeight", "Row height", 36 }, { "skillIconSize", "Icon size", 48 } } },
		};
		return lists;
	}

	inline const std::vector<RuntimeGroup>& RuntimeGroups()   // model.pages() Runtime: our own widgets, applied at the next start
	{
		static const std::vector<RuntimeGroup> groups{
			{ "Inventory and Magic header (our own lines, applied at the next game start)", { "Skin.rule", "Skin.guide" } },
			{ "Alchemy filters (our own heading, chips and Search effects box, applied at the next game start)",
				{ "Skin.alchemyHeading", "Skin.alchemyBox", "Skin.alchemyBoxLine", "Skin.alchemyClearLine" } },
			{ "Controller keyboard (Custom colours; game font and Inventory details parchment in all presets)",
				{ "Skin.keyboardKey", "Skin.keyboardEdge", "Skin.keyboardSelected", "Skin.keyboardSelectedEdge", "Skin.keyboardInk" } },
		};
		return groups;
	}

	inline constexpr double kTolerance = 1e-4;
	inline constexpr std::array<std::string_view, 3> kRetiredRoles{ "tabShadow", "tabLight", "ring" };   // written by older builders

	// ---- numbers as Python handles them ----
	inline double PyRound(double a_v) { return std::nearbyint(a_v) + 0.0; }   // round(): half to even (default FP mode)

	inline double Round4(double a_v)   // round(x, 4): correctly rounded on the exact binary value, like Python
	{
		char buffer[64];
		const auto end = std::to_chars(buffer, buffer + sizeof(buffer), a_v, std::chars_format::fixed, 4).ptr;
		double     out = 0;
		std::from_chars(buffer, end, out);
		return out + 0.0;
	}

	// custom_colours.number: str(int(v)) for whole numbers, else repr(float) (shortest round trip; exponent form below 1e-4)
	inline std::string Number(double a_v)
	{
		if (a_v == 0) return "0";
		char buffer[64];
		if (std::isfinite(a_v) && std::floor(a_v) == a_v && std::fabs(a_v) < 1e18) {
			const auto end = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<long long>(a_v)).ptr;
			return { buffer, end };
		}
		const auto  end = std::to_chars(buffer, buffer + sizeof(buffer), a_v, std::chars_format::scientific).ptr;
		std::string s(buffer, end);
		std::string sign;
		if (!s.empty() && s[0] == '-') {
			sign = "-";
			s.erase(0, 1);
		}
		const auto  e = s.find('e');
		if (e == std::string::npos) return sign + s;   // inf / nan (never in range)
		const int   exponent = std::stoi(s.substr(e + 1));
		std::string digits = s.substr(0, e);
		digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
		if (exponent >= -4 && exponent < 16) {
			if (exponent < 0) return sign + "0." + std::string(static_cast<size_t>(-exponent - 1), '0') + digits;
			const size_t whole = static_cast<size_t>(exponent) + 1;
			if (digits.size() <= whole) return sign + digits + std::string(whole - digits.size(), '0');
			return sign + digits.substr(0, whole) + "." + digits.substr(whole);
		}
		std::string out = sign + digits.substr(0, 1);
		if (digits.size() > 1) out += "." + digits.substr(1);
		const int magnitude = exponent < 0 ? -exponent : exponent;
		out += exponent < 0 ? "e-" : "e+";
		if (magnitude < 10) out += "0";
		return out + std::to_string(magnitude);
	}

	inline std::string_view Strip(std::string_view a_s)   // str.strip() for ASCII whitespace
	{
		constexpr std::string_view ws = " \t\r\n\f\v";
		const auto first = a_s.find_first_not_of(ws);
		if (first == std::string_view::npos) return {};
		return a_s.substr(first, a_s.find_last_not_of(ws) - first + 1);
	}

	inline bool PyFloat(std::string_view a_raw, double& a_out)   // float(text)
	{
		auto s = Strip(a_raw);
		if (s.size() > 1 && s[0] == '+' && s[1] != '-' && s[1] != '+') s.remove_prefix(1);
		if (s.empty()) return false;
		const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), a_out);
		return error == std::errc() && end == s.data() + s.size();
	}

	inline double Linear(double a_v)
	{
		const double c = a_v / 255;
		return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
	}

	inline double SrgbExact(double a_l)
	{
		a_l = std::max(0.0, std::min(1.0, a_l));
		const double c = a_l <= 0.0031308 ? a_l * 12.92 : 1.055 * std::pow(a_l, 1 / 2.4) - 0.055;
		return c * 255;
	}

	inline double Lum(const std::array<double, 3>& a_rgb) { return 0.2126 * a_rgb[0] + 0.7152 * a_rgb[1] + 0.0722 * a_rgb[2]; }

	inline bool Same(const Value& a_a, const Value& a_b)
	{
		if (a_a.n != a_b.n) return false;
		for (int i = 0; i < a_a.n; ++i)
			if (!(std::fabs(a_a.v[i] - a_b.v[i]) <= kTolerance)) return false;
		return true;
	}

	inline std::string Join(const Value& a_v)
	{
		std::string out;
		for (int i = 0; i < a_v.n; ++i) out += (i ? ", " : "") + Number(a_v.v[i]);
		return out;
	}

	namespace detail
	{
		inline Kind KindOf(customcolours::Kind a_k)
		{
			switch (a_k) {
			case customcolours::Kind::Rgb:  return Kind::Rgb;
			case customcolours::Kind::Unit: return Kind::Number;
			case customcolours::Kind::Grey: return Kind::Grey;
			case customcolours::Kind::Tint: return Kind::Rgba;
			default:                        return Kind::Scalar;
			}
		}

		inline double FromFloat(float a_f)   // the plugin's float default as the double Python reads from the same text
		{
			const auto text = customcolours::Number(a_f);
			double     out = 0;
			std::from_chars(text.data(), text.data() + text.size(), out);
			return out;
		}

		inline const std::vector<Entry>& DefaultEntries()   // file order: COLOURS, NUMBERS, TAB, SKIN
		{
			static const std::vector<Entry> entries = [] {
				std::vector<Entry> out;
				darkstyle::Style   style = darkstyle::kDefaults;
				for (const auto& e : customcolours::kEntries) {
					Entry entry;
					entry.section = e.section;
					entry.name = e.name;
					entry.help = e.help ? e.help : "";
					entry.key = entry.section == "Colours" ? entry.name : entry.section + "." + entry.name;
					entry.kind = KindOf(e.kind);
					entry.def.n = customcolours::Count(e.kind);
					const float* values = e.at(style);
					for (int i = 0; i < entry.def.n; ++i) entry.def.v[i] = FromFloat(values[i]);
					entry.value = entry.def;
					out.push_back(std::move(entry));
				}
				return out;
			}();
			return entries;
		}

		inline const Entry* FindDefault(std::string_view a_section, std::string_view a_name)
		{
			for (const auto& e : DefaultEntries())
				if (e.section == a_section && e.name == a_name) return &e;
			return nullptr;
		}

		inline bool IsTextRole(std::string_view a_key)
		{
			for (const auto& r : TextRoles())
				if (a_key == r.key) return true;
			return false;
		}

		struct SizeDefault
		{
			std::string key;
			double      def;
		};

		inline const std::vector<SizeDefault>& SizeDefaults()   // SIZES order: text role scales, row sizes, font sizes
		{
			static const std::vector<SizeDefault> sizes = [] {
				std::vector<SizeDefault> out;
				for (const auto& r : TextRoles()) out.push_back({ r.key, 100.0 });
				for (const auto& list : RowLists())
					for (const auto& row : list.rows) out.push_back({ row.key, row.def });
				for (const auto& f : FontSizes()) out.push_back({ std::string("font.") + f.member, f.def });
				return out;
			}();
			return sizes;
		}

		inline bool KnownSize(std::string_view a_key)
		{
			for (const auto& s : SizeDefaults())
				if (s.key == a_key) return true;
			return false;
		}

		inline bool Known(std::string_view a_section, std::string_view a_name)   // custom_colours.SECTIONS
		{
			if (a_section == "Colours" || a_section == "Tab" || a_section == "Skin") return FindDefault(a_section, a_name) != nullptr;
			if (a_section == "Roles") {
				for (const auto& r : Roles())
					if (a_name == r.key) return true;
				return false;
			}
			if (a_section == "Sizes") return KnownSize(a_name);
			return false;
		}

		inline std::string Quoted(std::string_view a_raw) { return "'" + std::string(a_raw) + "'"; }
	}

	// custom_colours.parse: one value with its section's count and range; false + the message otherwise
	inline bool ParseValue(std::string_view a_section, std::string_view a_name, std::string_view a_raw, Value& a_out, std::string& a_error)
	{
		const std::string name(a_name);
		auto              raw = Strip(a_raw.substr(0, a_raw.find(';')));
		if (a_section == "Sizes") {
			double v = 0;
			if (!PyFloat(raw, v)) {
				a_error = "CustomColours.ini: [Sizes] " + name + " = " + detail::Quoted(raw) + " is not a number";
				return false;
			}
			const bool   scale = detail::IsTextRole(a_name);
			const double low = scale ? 50 : 1, high = scale ? 200 : 400;
			if (!(low <= v && v <= high)) {
				a_error = "CustomColours.ini: [Sizes] " + name + " needs a number from " + Number(low) + " to " + Number(high) + ", found " + detail::Quoted(raw);
				return false;
			}
			a_out = Value{ 1, { v } };
			return true;
		}
		Value parts;
		std::vector<double> numbers;
		for (std::string_view rest = raw;;) {
			const auto comma = rest.find(',');
			double     v = 0;
			if (!PyFloat(rest.substr(0, comma), v)) {
				a_error = "CustomColours.ini: " + name + " = " + detail::Quoted(raw) + " is not a number list";
				return false;
			}
			numbers.push_back(v);
			if (comma == std::string_view::npos) break;
			rest.remove_prefix(comma + 1);
		}
		auto inRange = [&](double a_top) { return std::all_of(numbers.begin(), numbers.end(), [&](double p) { return 0 <= p && p <= a_top; }); };
		const auto* known = detail::FindDefault(a_section, a_name);
		if (a_section == "Skin") {
			const size_t size = known && known->def.n == 4 ? 4 : 1;
			if (numbers.size() != size || !inRange(1)) {
				a_error = "CustomColours.ini: [Skin] " + name + " needs " + std::to_string(size) + " number(s) from 0 to 1, found " + detail::Quoted(raw);
				return false;
			}
		} else if (a_section == "Roles" || (detail::FindDefault("Colours", a_name) && detail::FindDefault("Colours", a_name)->def.n == 3)) {
			if (numbers.size() != 3 || !inRange(255)) {
				a_error = "CustomColours.ini: " + std::string(a_section == "Roles" ? "[Roles] " : "") + name +
				          " needs three numbers from 0 to 255 (red, green, blue), found " + detail::Quoted(raw);
				return false;
			}
		} else {
			const double top = a_section == "Colours" && a_name == "detailsPage" ? 1.0 : 255.0;
			if (numbers.size() != 1 || !inRange(top)) {
				a_error = "CustomColours.ini: " + name + " needs one number from 0 to " + Number(top) + ", found " + detail::Quoted(raw);
				return false;
			}
		}
		parts.n = static_cast<int>(numbers.size());
		for (int i = 0; i < parts.n; ++i) parts.v[i] = numbers[i];
		a_out = parts;
		return true;
	}

	struct Found
	{
		std::string section, name;
		Value       value;
	};

	// custom_colours.read_entries with configparser's rules (strict: a duplicate section or entry, an entry before any section
	// or a line without '='/':' refuses the file). Unknown entries are skipped (notes); a broken value refuses the file.
	inline bool ReadEntries(std::string_view a_text, std::vector<Found>& a_found, std::string& a_error, std::vector<std::string>* a_notes = nullptr)
	{
		a_found.clear();
		if (a_text.starts_with("\xEF\xBB\xBF")) a_text.remove_prefix(3);
		struct Raw
		{
			std::string section, name, value;
		};
		std::vector<Raw>      raws;
		std::set<std::string> sections, names;
		std::string           section;
		bool                  inSection = false;
		int                   lineNumber = 0;
		while (!a_text.empty()) {
			const auto  stop = a_text.find_first_of("\r\n");
			const auto  line = a_text.substr(0, stop);
			if (stop == std::string_view::npos) a_text = {};
			else a_text.remove_prefix(stop + (a_text.substr(stop, 2) == "\r\n" ? 2 : 1));
			++lineNumber;
			const auto stripped = Strip(line);
			if (stripped.empty() || stripped.front() == ';' || stripped.front() == '#') continue;
			const auto close = stripped.rfind(']');
			if (stripped.front() == '[' && close != std::string_view::npos && close > 1) {
				section = std::string(stripped.substr(1, close - 1));
				if (!sections.insert(section).second) {
					a_error = "CustomColours.ini: section [" + section + "] appears twice (line " + std::to_string(lineNumber) + ")";
					return false;
				}
				names.clear();
				inSection = true;
				continue;
			}
			if (!inSection) {
				a_error = "CustomColours.ini: line " + std::to_string(lineNumber) + " comes before any [section]";
				return false;
			}
			const auto delimiter = stripped.find_first_of("=:");
			const auto name = delimiter == std::string_view::npos ? std::string_view{} : Strip(stripped.substr(0, delimiter));
			if (name.empty()) {
				a_error = "CustomColours.ini: line " + std::to_string(lineNumber) + " is not 'name = value'";
				return false;
			}
			if (!names.insert(std::string(name)).second) {
				a_error = "CustomColours.ini: [" + section + "] " + std::string(name) + " appears twice (line " + std::to_string(lineNumber) + ")";
				return false;
			}
			raws.push_back({ section, std::string(name), std::string(Strip(stripped.substr(delimiter + 1))) });
		}
		for (const auto& r : raws) {
			if (r.section == "Roles" && std::find(kRetiredRoles.begin(), kRetiredRoles.end(), r.name) != kRetiredRoles.end()) continue;
			if (!detail::Known(r.section, r.name)) {
				if (a_notes) a_notes->push_back("[" + r.section + "] " + r.name + " is not a known entry, ignored");
				continue;
			}
			Value v;
			if (!ParseValue(r.section, r.name, r.value, v, a_error)) return false;
			a_found.push_back({ r.section, r.name, v });
		}
		return true;
	}

	// app.set_preset: [Style] uPreset in MenusRevamped.ini; only that line changes (comments, other keys, BOM, line endings kept)
	inline std::string SetPreset(std::string_view a_file, int a_preset)
	{
		const bool bom = a_file.starts_with("\xEF\xBB\xBF");
		if (bom) a_file.remove_prefix(3);
		const std::string newline = a_file.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
		std::vector<std::string> lines;
		for (std::string_view rest = a_file; !rest.empty();) {
			const auto stop = rest.find_first_of("\r\n");
			lines.emplace_back(rest.substr(0, stop));
			if (stop == std::string_view::npos) break;
			rest.remove_prefix(stop + (rest.substr(stop, 2) == "\r\n" ? 2 : 1));
		}
		auto lower = [](std::string_view a_s) {
			std::string out(a_s);
			for (auto& c : out) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
			return out;
		};
		const std::string preset = "uPreset=" + std::to_string(a_preset);
		std::string       section;
		bool              done = false;
		int               styleAt = -1;
		for (size_t i = 0; i < lines.size(); ++i) {
			const auto stripped = Strip(lines[i]);
			if (stripped.starts_with("[") && stripped.find(']') != std::string_view::npos) {
				if (section == "style") {
					lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(i), preset);
					done = true;
					break;
				}
				section = lower(Strip(stripped.substr(1, stripped.find(']') - 1)));
				if (section == "style") styleAt = static_cast<int>(i);
				continue;
			}
			if (section == "style" && stripped.find('=') != std::string_view::npos && lower(Strip(stripped.substr(0, stripped.find('=')))) == "upreset") {
				lines[i] = preset;
				done = true;
				break;
			}
		}
		if (!done) {
			if (styleAt < 0) lines.push_back("[Style]");
			lines.push_back(preset);
		}
		std::string out = bom ? "\xEF\xBB\xBF" : "";
		for (size_t i = 0; i < lines.size(); ++i) out += (i ? newline : "") + lines[i];
		return out + newline;
	}

	class ColourModel
	{
	public:
		ColourModel()
		{
			std::string unused;
			Load({}, unused);
		}

		// The colour file's text (empty = no file). false: the file cannot be read (a_error names the entry), model unchanged.
		bool Load(std::string_view a_text, std::string& a_error, std::vector<std::string>* a_notes = nullptr)
		{
			std::vector<Found> found;
			if (!ReadEntries(a_text, found, a_error, a_notes)) return false;
			auto find = [&](std::string_view a_section, std::string_view a_name) -> const Value* {
				for (const auto& f : found)
					if (f.section == a_section && f.name == a_name) return &f.value;
				return nullptr;
			};
			_entries = detail::DefaultEntries();
			_index.clear();
			for (size_t i = 0; i < _entries.size(); ++i) _index[_entries[i].key] = i;
			_roleOf.clear();
			_roleDefaults.clear();
			_roles.clear();
			for (size_t r = 0; r < Roles().size(); ++r) {
				for (const auto& m : Roles()[r].members) _roleOf[m] = r;
				const auto& first = _entries[_index.at(Roles()[r].members[0])];
				_roleDefaults.push_back(ToSrgb(first.kind, first.def));
				std::array<double, 3> role{};
				if (const auto* v = find("Roles", Roles()[r].key))
					for (int i = 0; i < 3; ++i) role[i] = v->v[i];
				else
					for (int i = 0; i < 3; ++i) role[i] = PyRound(_roleDefaults[r][i]);
				_roles.push_back(role);
			}
			_own.clear();
			for (auto& e : _entries) {
				Value      derived;
				const bool hasRole = Derived(e.key, derived);
				if (const auto* v = find(e.section, e.name)) e.value = *v;
				else e.value = hasRole ? derived : e.def;
				if (hasRole && !Same(e.value, derived)) _own.insert(e.key);
			}
			_sizes.clear();
			for (const auto& s : detail::SizeDefaults()) _sizes[s.key] = s.def;
			for (const auto& f : found)
				if (f.section == "Sizes") _sizes[f.name] = f.value.v[0];
			_scaleOf.clear();
			_fontDefault.clear();
			for (const auto& r : TextRoles())
				for (const auto& m : r.members) _scaleOf["font." + m] = r.key;
			for (const auto& f : FontSizes()) _fontDefault[std::string("font.") + f.member] = f.def;
			_ownSizes.clear();
			for (const auto& [font, scale] : _scaleOf)
				if (std::fabs(_sizes[font] - DerivedFont(font)) > kTolerance) _ownSizes.insert(font);
			return true;
		}

		// ---- entries ----
		const std::vector<Entry>& Entries() const { return _entries; }
		const Entry&              ByKey(std::string_view a_key) const { return _entries[_index.at(std::string(a_key))]; }
		std::string               Text(std::string_view a_key) const { return Join(ByKey(a_key).value); }

		// A typed value with the colour file's ranges; false (a_error) keeps the old value. A role's element becomes own.
		bool SetText(std::string_view a_key, std::string_view a_text, std::string& a_error)
		{
			auto& e = Mutable(a_key);
			Value v;
			if (!ParseValue(e.section, e.name, a_text, v, a_error)) return false;
			e.value = v;
			Mark(e);
			return true;
		}

		std::array<int, 3> Swatch(std::string_view a_key) const   // the entry's colour in sRGB 0-255 (alpha ignored)
		{
			const auto&        e = ByKey(a_key);
			const auto         rgb = ToSrgb(e.kind, e.value);
			std::array<int, 3> out{};
			for (int i = 0; i < 3; ++i) out[i] = static_cast<int>(std::max(0.0, std::min(255.0, PyRound(rgb[i]))));
			return out;
		}

		// A picked sRGB colour (0-255) into the entry ([Skin] in linear, its alpha kept; a grey takes the luminance).
		void SetRgb(std::string_view a_key, std::array<double, 3> a_rgb)
		{
			auto& e = Mutable(a_key);
			for (auto& c : a_rgb) c = PyRound(c);
			switch (e.kind) {
			case Kind::Rgb:  e.value = Value{ 3, { a_rgb[0], a_rgb[1], a_rgb[2] } }; break;
			case Kind::Grey: e.value = Value{ 1, { PyRound(Lum(a_rgb)) } }; break;
			case Kind::Rgba: e.value = Value{ 4, { Round4(Linear(a_rgb[0])), Round4(Linear(a_rgb[1])), Round4(Linear(a_rgb[2])), e.value.v[3] } }; break;
			default:         e.value = Value{ 1, { Round4(Linear(Lum(a_rgb))) } }; break;
			}
			Mark(e);
		}

		// ---- roles (decisions 39, 43) ----
		const char* RoleOf(std::string_view a_key) const
		{
			const auto it = _roleOf.find(std::string(a_key));
			return it == _roleOf.end() ? nullptr : Roles()[it->second].key;
		}

		std::array<double, 3> RoleValue(std::string_view a_role) const { return _roles[RoleIndex(a_role)]; }

		std::string RoleText(std::string_view a_role) const
		{
			const auto& r = _roles[RoleIndex(a_role)];
			return Number(r[0]) + ", " + Number(r[1]) + ", " + Number(r[2]);
		}

		std::array<int, 3> RoleSwatch(std::string_view a_role) const
		{
			const auto&        r = _roles[RoleIndex(a_role)];
			std::array<int, 3> out{};
			for (int i = 0; i < 3; ++i) out[i] = static_cast<int>(std::max(0.0, std::min(255.0, PyRound(r[i]))));
			return out;
		}

		// A role colour (sRGB 0-255): every member that is not an own value follows.
		bool SetRole(std::string_view a_role, std::array<double, 3> a_rgb, std::string& a_error)
		{
			const size_t r = RoleIndex(a_role);
			for (auto& c : a_rgb) c = PyRound(c);
			for (const double c : a_rgb)
				if (!(0 <= c && c <= 255)) {
					a_error = std::string(Roles()[r].label) + " needs three numbers from 0 to 255";
					return false;
				}
			_roles[r] = a_rgb;
			for (const auto& key : Roles()[r].members)
				if (!_own.contains(key)) Derived(key, Mutable(key).value);
			return true;
		}

		bool SetRoleText(std::string_view a_role, std::string_view a_text, std::string& a_error)
		{
			Value v;
			if (!ParseValue("Roles", a_role, a_text, v, a_error)) return false;
			return SetRole(a_role, { v.v[0], v.v[1], v.v[2] }, a_error);
		}

		bool IsOwn(std::string_view a_key) const { return _own.contains(std::string(a_key)); }

		void ResetElement(std::string_view a_key)   // Advanced "Reset": back on its role
		{
			Value derived;
			if (Derived(a_key, derived)) {
				Mutable(a_key).value = derived;
				_own.erase(std::string(a_key));
			}
		}

		// ---- sizes (decisions 29, 33, 42, 45) ----
		double      Size(std::string_view a_key) const { return _sizes.at(std::string(a_key)); }
		std::string SizeText(std::string_view a_key) const { return Number(Size(a_key)); }
		bool        IsOwnSize(std::string_view a_key) const { return _ownSizes.contains(std::string(a_key)); }

		// A [Sizes] value with its range (false keeps the old value); a text role scale moves its fonts that are not own.
		bool SetSize(std::string_view a_key, std::string_view a_text, std::string& a_error)
		{
			Value v;
			if (!ParseValue("Sizes", a_key, a_text, v, a_error)) return false;
			const std::string key(a_key);
			_sizes[key] = v.v[0];
			if (_scaleOf.contains(key)) {
				if (std::fabs(v.v[0] - DerivedFont(key)) <= kTolerance) _ownSizes.erase(key);
				else _ownSizes.insert(key);
			}
			for (const auto& r : TextRoles())
				if (key == r.key)
					for (const auto& m : r.members) {
						const auto font = "font." + m;
						if (!_ownSizes.contains(font)) _sizes[font] = DerivedFont(font);
					}
			return true;
		}

		void ResetSize(std::string_view a_key)
		{
			const std::string key(a_key);
			if (_scaleOf.contains(key)) {
				_sizes[key] = DerivedFont(key);
				_ownSizes.erase(key);
			}
		}

		// ---- reset and save ----
		void ResetToDark()
		{
			for (auto& e : _entries) e.value = e.def;
			for (size_t r = 0; r < _roles.size(); ++r)
				for (int i = 0; i < 3; ++i) _roles[r][i] = PyRound(_roleDefaults[r][i]);
			_own.clear();
			for (const auto& s : detail::SizeDefaults()) _sizes[s.key] = s.def;
			_ownSizes.clear();
		}

		// custom_colours.render(values, skin, roles, sizes): the whole file the Python builder's Save writes
		std::string Render() const
		{
			std::string out =
				"; Menus Revamped - Custom colours.\n"
				"; [Colours]: red, green, blue (0-255) per element; detailsPage is one number. [Tab]: tab strip greys (0-255).\n"
				"; [Skin]: linear red, green, blue, alpha (0-1); the three scrollbar entries are single numbers.\n"
				"; Missing entries use Dark defaults. Rebuild for baked colours, including [Skin] pattern, ornament, emphasisInk and the scrollbar.\n"
				"; Other active [Skin] edits need a restart.\n"
				"; [Colours] pickerIcon, tabPaperDark, tabPaperLight, tabGlyph, enchantingTabPaper, enchantingTabIcon and the [Tab] entries are retained with no effect in this build (the tabs bake from a fixed charcoal palette).\n"
				"; [Skin] band, buttonFill, buttonRule and ring are retained with no effect in this build.\n";
			std::string section;
			for (const auto& e : _entries) {
				if (section != e.section) {
					section = e.section;
					out += "\n[" + section + "]\n";
				}
				std::string line = e.name + " = " + Join(e.value);
				if (line.size() < 36) line.append(36 - line.size(), ' ');
				out += line + "; " + e.help + "\n";
			}
			out += "\n[Roles]\n; The builder's role colours (sRGB 0-255); the elements above hold the values in use.\n";
			for (size_t r = 0; r < Roles().size(); ++r) out += std::string(Roles()[r].key) + " = " + RoleText(Roles()[r].key) + "\n";
			out += "\n[Sizes]\n; Text role scales in percent; row sizes and font sizes in points. Baked into MRLayout_P by Build.\n";
			for (const auto& s : detail::SizeDefaults()) out += s.key + " = " + Number(_sizes.at(s.key)) + "\n";
			return out;
		}

	private:
		static std::array<double, 3> ToSrgb(Kind a_kind, const Value& a_v)
		{
			switch (a_kind) {
			case Kind::Rgb:  return { a_v.v[0], a_v.v[1], a_v.v[2] };
			case Kind::Grey: return { a_v.v[0], a_v.v[0], a_v.v[0] };
			case Kind::Rgba: return { SrgbExact(a_v.v[0]), SrgbExact(a_v.v[1]), SrgbExact(a_v.v[2]) };
			default: {
				const double s = SrgbExact(a_v.v[0]);
				return { s, s, s };
			}
			}
		}

		static Value FromSrgb(Kind a_kind, std::array<double, 3> a_rgb, const Value& a_like)
		{
			for (auto& c : a_rgb) c = std::max(0.0, std::min(255.0, c));
			switch (a_kind) {
			case Kind::Rgb:  return Value{ 3, { PyRound(a_rgb[0]), PyRound(a_rgb[1]), PyRound(a_rgb[2]) } };
			case Kind::Grey: return Value{ 1, { PyRound(Lum(a_rgb)) } };
			case Kind::Rgba: return Value{ 4, { Round4(Linear(a_rgb[0])), Round4(Linear(a_rgb[1])), Round4(Linear(a_rgb[2])), a_like.v[3] } };
			default:         return Value{ 1, { Round4(Linear(Lum(a_rgb))) } };
			}
		}

		// What the element's role gives it (false: no role): its Dark default shifted by the role change.
		bool Derived(std::string_view a_key, Value& a_out) const
		{
			const auto it = _roleOf.find(std::string(a_key));
			if (it == _roleOf.end()) return false;
			const auto&           e = ByKey(a_key);
			std::array<double, 3> delta{};
			for (int i = 0; i < 3; ++i) delta[i] = _roles[it->second][i] - PyRound(_roleDefaults[it->second][i]);
			if (delta[0] == 0 && delta[1] == 0 && delta[2] == 0) {
				a_out = e.def;
				return true;
			}
			auto base = ToSrgb(e.kind, e.def);
			if (e.kind == Kind::Number || e.kind == Kind::Scalar || e.kind == Kind::Grey) {
				const double shift = (((0.0 + delta[0]) + delta[1]) + delta[2]) / 3;
				for (auto& c : base) c += shift;
			} else {
				for (int i = 0; i < 3; ++i) base[i] += delta[i];
			}
			a_out = FromSrgb(e.kind, base, e.def);
			return true;
		}

		double DerivedFont(const std::string& a_font) const
		{
			const double scale = _sizes.at(_scaleOf.at(a_font));
			return PyRound(_fontDefault.at(a_font) * scale / 100 * 2) / 2;
		}

		void Mark(const Entry& a_e)
		{
			Value derived;
			if (!Derived(a_e.key, derived)) return;
			if (Same(a_e.value, derived)) _own.erase(a_e.key);
			else _own.insert(a_e.key);
		}

		Entry& Mutable(std::string_view a_key) { return _entries[_index.at(std::string(a_key))]; }

		static size_t RoleIndex(std::string_view a_role)
		{
			for (size_t r = 0; r < Roles().size(); ++r)
				if (a_role == Roles()[r].key) return r;
			return 0;
		}

		std::vector<Entry>                 _entries;
		std::map<std::string, size_t>      _index;
		std::map<std::string, size_t>      _roleOf;
		std::vector<std::array<double, 3>> _roleDefaults;   // sRGB of each role's first member's Dark value (unrounded)
		std::vector<std::array<double, 3>> _roles;
		std::set<std::string>              _own;
		std::map<std::string, double>      _sizes;
		std::map<std::string, std::string> _scaleOf;        // font.<member> -> its text role scale key
		std::map<std::string, double>      _fontDefault;
		std::set<std::string>              _ownSizes;
	};
}
