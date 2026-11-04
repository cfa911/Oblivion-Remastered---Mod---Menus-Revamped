#pragma once
// Alchemy menu rules (IMP-MR-004/005/006): pure logic, no game types, so tests/alchemy_rules_tests.cpp can run on the host.
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace alchemyrules
{
	struct Ingredient
	{
		std::string              name;          // the row's display name (may carry a tag mod prefix such as "[Ingr] ")
		int                      count = 0;     // how many the player carries
		float                    weight = 0;    // the form's weight; < 0 when unknown
		int                      value = 0;     // the form's value; < 0 when unknown
		std::vector<std::string> knownEffects;  // display names of the effects the player knows
	};

	enum class SortKey { Name, Count, Weight, Value, KnownEffects, kCount };

	inline const char* SortLabel(SortKey a_key)
	{
		switch (a_key) {
		case SortKey::Count:        return "Count";
		case SortKey::Weight:       return "Weight";
		case SortKey::Value:        return "Value";
		case SortKey::KnownEffects: return "Known effects";
		default:                    return "Name";
		}
	}

	inline SortKey NextSort(SortKey a_key)
	{
		const int next = (static_cast<int>(a_key) + 1) % static_cast<int>(SortKey::kCount);
		return static_cast<SortKey>(next);
	}

	inline SortKey SortFromIni(int a_index)
	{
		return a_index >= 0 && a_index < static_cast<int>(SortKey::kCount) ? static_cast<SortKey>(a_index) : SortKey::Name;
	}

	namespace detail
	{
		inline bool Space(char c) { return c == ' ' || c == '\t'; }

		// leading bytes that are not ASCII letters, digits or '[' (icon glyphs a tag mod puts first, e.g. U+263A)
		inline std::string_view SkipGlyphs(std::string_view s)
		{
			std::size_t i = 0;
			while (i < s.size()) {
				const auto c = static_cast<unsigned char>(s[i]);
				if (c < 0x80 && (std::isalnum(c) || c == '[')) break;
				++i;
			}
			return s.substr(i);
		}

		inline std::string_view Trim(std::string_view s)
		{
			while (!s.empty() && Space(s.front())) s.remove_prefix(1);
			while (!s.empty() && Space(s.back())) s.remove_suffix(1);
			return s;
		}

		// "Name (12)" -> "Name": only a bracketed whole number at the very end
		inline std::string_view DropCount(std::string_view s)
		{
			s = Trim(s);
			if (s.size() < 3 || s.back() != ')') return s;
			const auto open = s.rfind('(');
			if (open == std::string_view::npos || open + 2 > s.size() - 1) return s;
			for (std::size_t i = open + 1; i + 1 < s.size(); ++i) {
				if (!std::isdigit(static_cast<unsigned char>(s[i]))) return s;
			}
			return Trim(s.substr(0, open));
		}

		// glyphs, then "[Tag] ", then spaces
		inline std::string_view DropPrefix(std::string_view s)
		{
			s = Trim(SkipGlyphs(Trim(s)));
			if (!s.empty() && s.front() == '[') {
				const auto close = s.find(']');
				if (close != std::string_view::npos) s = Trim(s.substr(close + 1));
			}
			return s;
		}

		inline std::string Lower(std::string_view s)
		{
			std::string out(s);
			for (auto& c : out) {
				if (static_cast<unsigned char>(c) < 0x80) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return out;
		}
	}

	// An effect or item name reduced for matching: leading icon glyphs, a "[Tag] " prefix and a trailing " (N)" removed, lower case.
	inline std::string MatchKey(std::string_view a_display) { return detail::Lower(detail::DropCount(detail::DropPrefix(a_display))); }

	// The order to show a_items in: indices into a_items. Ties fall back to the name, then the original order.
	inline std::vector<int> SortOrder(const std::vector<Ingredient>& a_items, SortKey a_key)
	{
		std::vector<int> order(a_items.size());
		std::vector<std::string> names(a_items.size());
		for (int i = 0; i < static_cast<int>(order.size()); ++i) {
			order[i] = i;
			names[i] = MatchKey(a_items[i].name);
		}
		const auto byName = [&](int a, int b) { return names[a] < names[b]; };
		std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
			const auto& x = a_items[a];
			const auto& y = a_items[b];
			switch (a_key) {
			case SortKey::Count:
				if (x.count != y.count) return x.count > y.count;
				break;
			case SortKey::Weight:
				if ((x.weight < 0) != (y.weight < 0)) return y.weight < 0;   // unknown last
				if (x.weight != y.weight) return x.weight < y.weight;
				break;
			case SortKey::Value:
				if ((x.value < 0) != (y.value < 0)) return y.value < 0;
				if (x.value != y.value) return x.value > y.value;
				break;
			case SortKey::KnownEffects:
				if (x.knownEffects.size() != y.knownEffects.size()) return x.knownEffects.size() > y.knownEffects.size();
				break;
			default:
				break;
			}
			return byName(a, b);
		});
		return order;
	}

	// Per effect (MatchKey): how many ingredients have it among their known effects.
	inline std::unordered_map<std::string, int> EffectCounts(const std::vector<Ingredient>& a_items)
	{
		std::unordered_map<std::string, int> counts;
		for (const auto& item : a_items) {
			std::unordered_set<std::string> seen;
			for (const auto& effect : item.knownEffects) {
				auto key = MatchKey(effect);
				if (!key.empty() && seen.insert(key).second) ++counts[key];
			}
		}
		return counts;
	}

	// "Fire Shield" -> "Fire Shield (7)"; an earlier " (N)" suffix is replaced, never stacked.
	inline std::string WithCount(std::string_view a_display, int a_count)
	{
		return std::string(detail::DropCount(a_display)) + " (" + std::to_string(a_count) + ")";
	}

	// Where our labels go in the Ingredients title row (viewport units). The sort label sits between the "Ingredients"
	// title and the native Filters button; the active-filter line gets its own line between the title row and the list.
	struct Box { double x = 0, y = 0, w = 0, h = 0; };
	struct TitleLayout
	{
		double sortX = 0, sortY = 0, sortMaxWidth = 0;   // left edge, vertical centre, room before the Filters button
		double chipsX = 0, chipsY = 0, chipsRight = 0;   // the active-filter line: left edge, vertical centre, right limit
	};
	// The filter item's effect-name field, found at run time: of the texts read from the item (offset, text), the
	// first whose MatchKey equals the row's; -1 when none does (VModernAlchemyEffect has no reflected fields).
	inline int LearnTextOffset(const std::vector<std::pair<int, std::string>>& a_texts, std::string_view a_row)
	{
		const auto want = MatchKey(a_row);
		if (want.empty()) return -1;
		for (const auto& [offset, text] : a_texts) {
			if (MatchKey(text) == want) return offset;
		}
		return -1;
	}

	// The same from many items at once (no row event needed): per item its (offset, text) pairs, and the texts the
	// displayed rows show; the offset whose text matches a row for the most items, -1 when none matches.
	inline int LearnTextOffsetFromRows(const std::vector<std::vector<std::pair<int, std::string>>>& a_items, const std::vector<std::string>& a_rows)
	{
		std::unordered_set<std::string> shown;
		for (const auto& r : a_rows) {
			if (auto k = MatchKey(r); !k.empty()) shown.insert(std::move(k));
		}
		std::unordered_map<int, int> hits;
		for (const auto& texts : a_items) {
			for (const auto& [offset, text] : texts) {
				if (shown.count(MatchKey(text))) ++hits[offset];
			}
		}
		int best = -1, bestHits = 0;
		for (const auto& [offset, n] : hits) {
			if (n > bestHits || (n == bestHits && offset < best)) { best = offset; bestHits = n; }
		}
		return best;
	}

	// The parchment strip added above the Ingredients panel (owner 2026-11-04, extra space for the active-filter buttons):
	// placed as in the owner's PSD, at fixed offsets from the panel's background widget (a_dx, a_dy, a_dw in viewport
	// units), its height keeping the image's aspect (h / w). chipsY: the centre of the new space for the buttons.
	struct StripLayout { Box box; double chipsY = 0; };
	inline StripLayout PlaceStrip(Box a_panel, double a_aspect, double a_dx, double a_dy, double a_dw)
	{
		StripLayout l;
		const double w = a_panel.w + a_dw;
		l.box = { a_panel.x + a_dx, a_panel.y + a_dy, w, w * a_aspect };
		l.chipsY = l.box.y + l.box.h * 0.42;   // the middle of the strip's solid part (it is solid to 62% of its height)
		return l;
	}

	// The strip greys with the Ingredients panel while the Filters panel is open (the menu shows ListGreyedImage over the
	// panel). The multiplier is linear colour: the panel's screen ratio 0.71 (owner screenshots 2026-11-04) is 0.71^2.2 = 0.47.
	inline float StripTint(bool a_greyed) { return a_greyed ? 0.47f : 1.0f; }

	// Active-filter buttons (owner 2026-11-04: outline and a background that highlights): the box round a label whose
	// left edge is a_x and vertical centre a_centreY, and which button a point is on (-1 none).
	inline Box ButtonBox(double a_x, double a_centreY, double a_w, double a_h, double a_padX, double a_padY)
	{
		return { a_x - a_padX, a_centreY - a_h * 0.5 - a_padY, a_w + a_padX * 2, a_h + a_padY * 2 };
	}

	inline int HitIndex(const std::vector<Box>& a_boxes, double a_x, double a_y)
	{
		for (std::size_t i = 0; i < a_boxes.size(); ++i) {
			const auto& b = a_boxes[i];
			if (b.w > 0 && b.h > 0 && a_x >= b.x && a_x < b.x + b.w && a_y >= b.y && a_y < b.y + b.h) return static_cast<int>(i);
		}
		return -1;
	}

	// a text widget stretched to fill its row measures wider than its text: use the text's own width when known
	inline double TextWidth(double a_measured, double a_desired) { return a_desired > 0 && a_desired < a_measured ? a_desired : a_measured; }

	inline TitleLayout PlaceTitleRow(Box a_title, Box a_filters, double a_listTop, double a_gap)
	{
		TitleLayout l;
		l.sortX = a_title.x + a_title.w + a_gap * 2;
		l.sortY = a_title.y + a_title.h * 0.5;
		l.sortMaxWidth = (std::max)(0.0, a_filters.x - a_gap - l.sortX);
		l.chipsX = a_title.x;
		l.chipsRight = a_filters.x + a_filters.w;
		const double rowBottom = (std::max)(a_title.y + a_title.h, a_filters.y + a_filters.h);
		l.chipsY = a_listTop > rowBottom ? (rowBottom + a_listTop) * 0.5 : rowBottom + 16;   // list unknown: just under the row
		return l;
	}

	// RMI-021: the view model's ingredient data is read only once this opening's list is built - the game's own
	// UpdateItemsList ran, or a_settleMs passed since the menu became active (when that call came before we watched it).
	struct OpenState
	{
		bool open = false;
		bool built = false;
		long long openedAt = 0;
		void Opened(long long a_now) { open = true; built = false; openedAt = a_now; }
		void Closed() { open = false; built = false; }
		void ListBuilt() { built = open; }
		bool CanRead(long long a_now, long long a_settleMs) const { return open && (built || a_now - openedAt >= a_settleMs); }
	};

	// RMI-022: the counts need every ingredient's effect texts: read them when the menu opens and when the number of
	// ingredients changes (brewing used one up), not on every filter click. Only the known-effects sort reads effects.
	inline bool NeedCounts(bool a_opened, int a_lastNum, int a_num) { return a_opened || a_lastNum != a_num; }
	inline bool SortReadsEffects(SortKey a_key) { return a_key == SortKey::KnownEffects; }

	// RMI-023: the ingredient's effect struct is read from the array property, not assumed; its effect-name field is
	// EffectName (ModernEffectItemData) or Effect (OriginalInventoryMenuIngredientEffects); -1 when neither exists.
	inline int EffectNameField(const std::vector<std::pair<std::string, int>>& a_fields)
	{
		for (const char* want : { "EffectName", "Effect" }) {
			for (const auto& [name, offset] : a_fields) {
				if (name == want) return offset;
			}
		}
		return -1;
	}

	// RMI-023 (2026-11-04 run: the array holds ModernItemEffectProperties, 0x40 bytes, no EffectName/Effect field): pick
	// the effect-name field by size - the known names first, then the first text-sized field whose name holds "Name", then
	// the first text-sized field. a_fields: name, offset, size. -1 when no field is text-sized.
	struct FieldInfo { std::string name; int offset = -1; int size = 0; };
	inline int TextField(const std::vector<FieldInfo>& a_fields, int a_textSize)
	{
		for (const char* want : { "EffectName", "Effect" }) {
			for (const auto& f : a_fields) {
				if (f.name == want && f.size == a_textSize) return f.offset;
			}
		}
		for (const auto& f : a_fields) {
			if (f.size == a_textSize && f.name.find("Name") != std::string::npos) return f.offset;
		}
		for (const auto& f : a_fields) {
			if (f.size == a_textSize) return f.offset;
		}
		return -1;
	}

	// the effect's "unknown" flag: offset and whether it is inverted (bIsKnown); offset -1 when the struct has none
	inline std::pair<int, bool> UnknownFlag(const std::vector<FieldInfo>& a_fields)
	{
		for (const auto& f : a_fields) {
			if (f.name == "bIsUnknown" || f.name == "bIsUnknow" || f.name == "bUnknown") return { f.offset, false };   // bIsUnknow: the game's spelling
		}
		for (const auto& f : a_fields) {
			if (f.name == "bIsKnown" || f.name == "bKnown") return { f.offset, true };
		}
		return { -1, false };
	}

	// RMI-022 follow-up (owner 2026-11-04: the "+N" entry flickered): how many filter buttons fit in a_room, given each
	// button's width (text width + padding), the gap, and the width of the "+N" marker; all fit -> a_widths.size().
	// Deterministic: the same widths always give the same answer.
	inline std::size_t FitButtons(const std::vector<double>& a_widths, double a_room, double a_gap, double a_marker)
	{
		const std::size_t n = a_widths.size();
		double total = 0;
		for (std::size_t i = 0; i < n; ++i) total += a_widths[i] + (i ? a_gap : 0);
		if (total <= a_room) return n;
		std::size_t k = 0;
		double      used = 0;
		for (std::size_t i = 0; i < n; ++i) {
			const double next = used + (i ? a_gap : 0) + a_widths[i];
			if (next + a_gap + a_marker > a_room) break;
			used = next;
			k = i + 1;
		}
		return n ? (std::max)(k, std::size_t{ 1 }) : 0;
	}

	// Owner 2026-11-04: the active-filter buttons wrap onto a second row under the first (Clear all leftmost on row 0);
	// the "+N" marker follows the last button shown, on the last row. Per shown button: its row and its x offset from the
	// row start (a_widths include padding). The first button always shows. Deterministic.
	struct ButtonFlow
	{
		std::vector<int>    row;
		std::vector<double> x;
		bool                more = false;
		int                 moreRow = 0;
		double              moreX = 0;
	};
	inline ButtonFlow FlowButtons(const std::vector<double>& a_widths, double a_room, double a_gap, double a_marker, int a_rows)
	{
		const auto pass = [&](bool a_reserve, ButtonFlow& a_out) {
			a_out = {};
			int    r = 0;
			double used = 0;   // width taken on row r (0 = empty)
			bool   empty = true;
			for (std::size_t i = 0; i < a_widths.size(); ++i) {
				const bool last = r == a_rows - 1;
				double     start = empty ? 0 : used + a_gap;
				const auto need = [&](double a_start) { return a_start + a_widths[i] + (a_reserve && r == a_rows - 1 ? a_gap + a_marker : 0); };
				if (!empty && need(start) > a_room) {
					if (last) break;
					++r;
					empty = true;
					used = 0;
					start = 0;
				}
				if (empty && i > 0 && need(start) > a_room && r == a_rows - 1 && a_reserve) break;
				if (!empty && need(start) > a_room) break;
				a_out.row.push_back(r);
				a_out.x.push_back(start);
				used = start + a_widths[i];
				empty = false;
			}
			a_out.moreRow = (std::max)(0, a_rows - 1);
			a_out.moreX = a_out.moreRow == r && !empty ? used + a_gap : 0;
			return a_out.row.size() == a_widths.size();
		};
		ButtonFlow flow;
		if (a_widths.empty() || a_rows <= 0 || pass(false, flow)) return flow;
		pass(true, flow);
		flow.more = true;
		return flow;
	}

	// RMI-022: a live text object's first field is its vtable, which lies inside the game executable's image; a value
	// that only looks like a pointer (0x8000000000 in the 2026-11-04 log) or a wrong offset fails this.
	inline bool InImage(unsigned long long a_value, unsigned long long a_base, unsigned long long a_size)
	{
		return a_value >= a_base && a_value - a_base < a_size;
	}

	// RMI-021: an FText's two pointers (text data, reference controller) look like user-space heap pointers
	inline bool PlausibleText(unsigned long long a_data, unsigned long long a_controller)
	{
		const auto ok = [](unsigned long long p) { return p >= 0x10000ull && p < 0x00007FFFFFFF0000ull && (p & 7) == 0; };
		return ok(a_data) && ok(a_controller);
	}

	// RMI-024 (game bug, vanilla too): a filter row reused on scroll comes back unselected even when its effect is an
	// active filter. True when the row shows a_rowText unselected while its effect is among a_selected (any form: tag
	// prefix, count suffix and case do not matter).
	inline bool ShouldRestoreSelected(std::string_view a_rowText, bool a_rowSelected, const std::vector<std::string>& a_selected)
	{
		if (a_rowSelected) return false;
		const auto key = MatchKey(a_rowText);
		if (key.empty()) return false;
		for (const auto& name : a_selected) {
			if (MatchKey(name) == key) return true;
		}
		return false;
	}

	// The text of one active-filter entry: the name without icon glyphs or tag prefix, as shown on screen.
	inline std::string ChipLabel(std::string_view a_display) { return std::string(detail::DropCount(detail::DropPrefix(a_display))); }
}
