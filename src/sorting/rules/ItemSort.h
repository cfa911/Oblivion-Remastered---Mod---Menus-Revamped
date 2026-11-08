#pragma once

// Own sort bars replacing the native sorts (owner 2026-11-07, KB mods/menus-revamped/evidence/own-sorts-20261107.md).
// Pure rules, no engine types: unit-tested in tests/item_sort_tests.cpp.

#include "sorting/rules/ItemTags.h"
#include "sorting/rules/MagicRows.h"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace itemsort
{
	enum class Key : unsigned { Name, Health, Damage, Armor, Weight, Value, Cost, EffectValue, Category, School, Magnitude, kCount };

	struct State
	{
		Key  key = Key::Name;
		bool descending = false;
		int  group = -1;   // >= 0: the category sort of that filter (picker kind); the key is then unused

		bool operator==(const State&) const = default;
	};

	// One list row's sortable values (OriginalInventoryMenuItemProperties); a negative number = none (sorts last).
	struct Row
	{
		std::string name;
		float       price = -1, weight = -1, damage = -1, armor = -1, health = -1, cost = -1;
		float       effectValue = -1, category = -1, school = -1, magnitude = -1;   // Magic Active Effects
		int         group = -1;   // category-sort rank of the row (-1 = none: sorts last)
	};

	// The sorts a tab offers, left to right (owner screenshots of the 1.4.1 native bars).
	inline std::vector<Key> Keys(itemtags::Tab a_tab)
	{
		using itemtags::Tab;
		switch (a_tab) {
		case Tab::Weapons:  return { Key::Name, Key::Health, Key::Damage, Key::Weight, Key::Value };
		case Tab::Gear:     return { Key::Name, Key::Health, Key::Armor, Key::Weight, Key::Value };
		case Tab::AllItems: return { Key::Name, Key::Health, Key::Damage, Key::Armor, Key::Weight, Key::Value };
		default:            return { Key::Name, Key::Weight, Key::Value };   // Alchemy (Potions page), Misc
		}
	}

	inline bool Offers(itemtags::Tab a_tab, Key a_key)
	{
		const auto keys = Keys(a_tab);
		return std::find(keys.begin(), keys.end(), a_key) != keys.end();
	}

	// The game's own sort textures (KB docs/inventory/internals.md "Native sort header icon textures"), loaded at run time.
	inline const wchar_t* TexturePath(Key a_key)
	{
		switch (a_key) {
		case Key::Health: return L"/Game/Art/UI/Icons/Icon_Stats/T_Property_1_Health.T_Property_1_Health";
		case Key::Damage: return L"/Game/Art/UI/Icons/Icon_Stats/T_Property_1_Attack.T_Property_1_Attack";
		case Key::Armor:  return L"/Game/Art/UI/Icons/Icon_Stats/T_Property_1_defense.T_Property_1_defense";
		case Key::Weight: return L"/Game/Art/UI/Icons/Icon_Stats/T_Property_1_Weight.T_Property_1_Weight";
		case Key::Value:  return L"/Game/Art/UI/Icons/Icon_Stats/T_Property_1_MoneyV2.T_Property_1_MoneyV2";
		case Key::Cost:   return L"/Game/Art/UI/Icons/Icon_Stats/T_Property_1_cost.T_Property_1_cost";
		// Active Effects icons (KB tab-pages "Magic sort buttons": goodbad masks, LesserPower star, school scroll, magnitude).
		case Key::EffectValue: return L"/Game/Art/UI/Icons/Icon_Stats/T_Property_1_goodbad.T_Property_1_goodbad";
		case Key::Category:    return L"/Game/Art/UI/Icons/Icon_Stats/T_Property_1_LesserPower.T_Property_1_LesserPower";
		case Key::School:      return L"/Game/Art/UI/Icons/Icon_Stats/T_Property_1_school.T_Property_1_school";
		case Key::Magnitude:   return L"/Game/Art/UI/Icons/Icon_Stats/T_Property_1_magnitude.T_Property_1_magnitude";
		default:          return L"/Game/Art/UI/Icons/Icon_Stats/T_Property_1_sorting.T_Property_1_sorting";
		}
	}
	constexpr const wchar_t* kActiveBackground = L"/Game/Art/UI/Common/Filter/T_Filter_Icon_Background_Small.T_Filter_Icon_Background_Small";
	constexpr const wchar_t* kArrowUp = L"/Game/Art/UI/Common/T_ArrowStat_indicationUp.T_ArrowStat_indicationUp";
	constexpr const wchar_t* kArrowDown = L"/Game/Art/UI/Common/T_ArrowStat_indicationDown.T_ArrowStat_indicationDown";

	// The native sort-button wrappers in a filter ScrollBox are named after EOriginalInventoryMenuSortType.
	inline bool IsNativeSortWrapper(std::string_view a_name)
	{
		for (std::string_view n : { "Name", "Price", "Weight", "ArmorRating", "WeaponDamage", "ItemHealth", "ItemCharge", "SpellCost", "SpellPrice" })
			if (a_name == n) return true;
		return false;
	}

	// Command ids for our sort buttons (picker commands use -1..-6 and choices >= 0): click (sort / reverse), or a
	// controller Up / Down on a focused sort (set ascending / descending, like the native vertical input).
	enum class Action : int { Click, Ascending, Descending };
	constexpr int kSortCommandBase = -100;
	constexpr int kActionStride = 20;   // > Key::kCount
	static_assert(static_cast<int>(Key::kCount) < kActionStride);
	inline int SortCommand(Key a_key, Action a_action = Action::Click)
	{
		return kSortCommandBase - static_cast<int>(a_action) * kActionStride - static_cast<int>(a_key);
	}
	inline bool IsSortCommand(int a_cmd)
	{
		const int offset = kSortCommandBase - a_cmd;
		return offset >= 0 && offset < 3 * kActionStride && offset % kActionStride < static_cast<int>(Key::kCount);
	}
	inline Key    SortCommandKey(int a_cmd) { return static_cast<Key>((kSortCommandBase - a_cmd) % kActionStride); }
	inline Action SortCommandAction(int a_cmd) { return static_cast<Action>((kSortCommandBase - a_cmd) / kActionStride); }

	// First click: A-Z and categories (good/bad, category, school) ascending, amounts highest first (the Alchemy rule).
	inline bool DefaultDescending(Key a_key)
	{
		return a_key != Key::Name && a_key != Key::EffectValue && a_key != Key::Category && a_key != Key::School;
	}

	// A click on a sort: the same one reverses, another one starts in its default direction.
	inline State Click(State a_state, Key a_key)
	{
		if (a_state.key == a_key) return { a_key, !a_state.descending };
		return { a_key, DefaultDescending(a_key) };
	}

	// A remembered sort the tab does not offer falls back to A-Z ascending.
	inline State Effective(State a_state, itemtags::Tab a_tab)
	{
		return a_state.group >= 0 || Offers(a_tab, a_state.key) ? a_state : State{};
	}

	inline float Number(const Row& a_row, Key a_key)
	{
		switch (a_key) {
		case Key::Health: return a_row.health;
		case Key::Damage: return a_row.damage;
		case Key::Armor:  return a_row.armor;
		case Key::Weight: return a_row.weight;
		case Key::Value:  return a_row.price;
		case Key::Cost:   return a_row.cost;
		case Key::EffectValue: return a_row.effectValue;
		case Key::Category:    return a_row.category;
		case Key::School:      return a_row.school;
		case Key::Magnitude:   return a_row.magnitude;
		default:          return -1;
		}
	}

	inline std::string FoldName(const std::string& a_name)
	{
		std::string out(a_name);
		for (auto& c : out)
			if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
		return out;
	}

	// The display order: a permutation of the row indices. Missing numbers last in both directions; ties by name
	// ascending, then same-name rows by value highest first, then weight highest first (RMI-049), then the original order.
	// A category sort: groups in order; inside a group A-Z, same names by value highest first, then weight highest first
	// (owner 2026-11-08, the default), or with a_valueFirst ([Picker] bCategorySortValueFirst=1, the RMI-049 order) value
	// highest first, equal values A-Z, then weight. Descending is the exact reverse. Rows without a group stay last both
	// ways; rows without a value stay after the priced ones they tie with.
	inline std::vector<int> Order(std::span<const Row> a_rows, State a_state, bool a_valueFirst = false)
	{
		std::vector<int>         order(a_rows.size());
		std::vector<std::string> names(a_rows.size());
		for (int i = 0; i < static_cast<int>(order.size()); ++i) {
			order[i] = i;
			names[i] = FoldName(a_rows[i].name);
		}
		const bool d = a_state.descending;
		// Highest first, missing last; 0 when equal.
		const auto higher = [](float x, float y) -> int {
			if ((x < 0) != (y < 0)) return y < 0 ? -1 : 1;
			if (x < 0 || x == y) return 0;
			return x > y ? -1 : 1;
		};
		const auto sameName = [&](int a, int b) {
			if (const int c = higher(a_rows[a].price, a_rows[b].price)) return c < 0;
			return higher(a_rows[a].weight, a_rows[b].weight) < 0;
		};
		const auto byName = [&](int a, int b, bool a_desc) {
			if (names[a] != names[b]) return (names[a] < names[b]) != a_desc;
			return sameName(a, b);
		};
		std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
			if (a_state.group >= 0) {
				const int x = a_rows[a].group, y = a_rows[b].group;
				if ((x < 0) != (y < 0)) return y < 0;
				if (x != y) return (x < y) != d;
				if (!a_valueFirst && names[a] != names[b]) return (names[a] < names[b]) != d;
				const float pa = a_rows[a].price, pb = a_rows[b].price;
				if ((pa < 0) != (pb < 0)) return pb < 0;
				if (const int c = higher(pa, pb)) return (c < 0) != d;
				if (a_valueFirst && names[a] != names[b]) return (names[a] < names[b]) != d;
				if (const int c = higher(a_rows[a].weight, a_rows[b].weight)) return (c < 0) != d;
				return false;
			}
			if (a_state.key == Key::Name) return byName(a, b, d);
			const float x = Number(a_rows[a], a_state.key), y = Number(a_rows[b], a_state.key);
			if ((x < 0) != (y < 0)) return y < 0;
			if (x >= 0 && x != y) return (x < y) != d;
			return byName(a, b, false);
		});
		return order;
	}

	// The state after a command on a tab (the remembered state first falls back to what the tab offers).
	inline State Apply(State a_state, itemtags::Tab a_tab, Key a_key, Action a_action)
	{
		auto current = Effective(a_state, a_tab);
		if (current.group >= 0) current = { Key::kCount, false };   // a sort button switches the category sort off
		switch (a_action) {
		case Action::Ascending:  return { a_key, false };
		case Action::Descending: return { a_key, true };
		default:                 return Click(current, a_key);
		}
	}

	// ---- Magic spell pages (Self, Touch, Target, All). The game offers [0 Name, 2 ItemCategory, 4 School, 6 Cost]
	// there (KB tab-pages "Magic sort types per page"); ItemCategory and School are already our type/school pickers.
	inline std::vector<Key> MagicKeys() { return { Key::Name, Key::Cost }; }

	// Which of our sorts replaces a native Magic sort wrapper in place (by wrapper name); -1 = keep the native one.
	inline int MagicNativeKey(std::string_view a_name)
	{
		if (a_name == "Name") return static_cast<int>(Key::Name);
		if (a_name == "Cost") return static_cast<int>(Key::Cost);
		return -1;
	}

	// ELegacyMagicMenuSortType values used to make the game refill the Magic list (inferred: Name 0, Cost 6).
	inline int NativeSortType(Key a_key) { return a_key == Key::Name ? 0 : a_key == Key::Category ? 2 : a_key == Key::Cost ? 6 : -1; }

	inline State Effective(State a_state, std::span<const Key> a_keys)
	{
		return a_state.group >= 0 || std::find(a_keys.begin(), a_keys.end(), a_state.key) != a_keys.end() ? a_state : State{};
	}

	inline State Apply(State a_state, std::span<const Key> a_keys, Key a_key, Action a_action)
	{
		auto current = Effective(a_state, a_keys);
		if (current.group >= 0) current = { Key::kCount, false };
		switch (a_action) {
		case Action::Ascending:  return { a_key, false };
		case Action::Descending: return { a_key, true };
		default:                 return Click(current, a_key);
		}
	}

	// ---- Magic Active Effects (page 4): the game offers [0, 1, 4, 2, 3]; all five become ours (owner screenshot order).
	inline std::vector<Key> ActiveKeys() { return { Key::Name, Key::EffectValue, Key::Category, Key::School, Key::Magnitude }; }

	inline int ActiveNativeKey(std::string_view a_name)
	{
		if (a_name == "Name") return static_cast<int>(Key::Name);
		if (a_name == "EffectValue") return static_cast<int>(Key::EffectValue);
		if (a_name == "ItemCategory") return static_cast<int>(Key::Category);
		if (a_name == "School") return static_cast<int>(Key::School);
		if (a_name == "Magnitude") return static_cast<int>(Key::Magnitude);
		return -1;
	}

	// ---- Category sort on the filters (owner 2026-11-07, KB evidence own-sorts-20261107 "Brainstorm: sort by our filters").
	// Group order = the popup's choice order (choice ids are in the owner's order), A-Z inside a group; only one sort is
	// active at a time (a sort button or one filter); the filters themselves still combine.

	// A filter's category sort: click = start ascending / reverse; controller Up / Down = ascending / descending.
	inline State ApplyGroup(State a_state, int a_group, Action a_action)
	{
		switch (a_action) {
		case Action::Ascending:  return { Key::Name, false, a_group };
		case Action::Descending: return { Key::Name, true, a_group };
		default:                 return { Key::Name, a_state.group == a_group && !a_state.descending, a_group };
		}
	}

	// The first state of a tab: grouped by its category filter ([Picker] bCategorySort=1) or A-Z.
	inline State Default(bool a_categorySort, int a_group) { return a_categorySort ? State{ Key::Name, false, a_group } : State{}; }

	// The first of the row's choices (1..count-1) in the tab's order; -1 none.
	inline int FirstChoice(itemtags::Tab a_tab, std::uint32_t a_tags)
	{
		const auto count = itemtags::Info(a_tab).count;
		for (unsigned choice = 1; choice < count && choice <= 28; ++choice)
			if ((a_tags >> (choice - 1)) & 1u) return static_cast<int>(choice);
		return -1;
	}

	// An item row's group from its classification tags (bits 28-31: category). All items: the category in tab order,
	// then that tab's own order (unclassified rows of a category after its groups); a row without a category: none.
	// Alchemy follows its popup tree (RMI-051): Tools 0, Potions by Potion type 1..7 (no type 8), Poisons by Poison type
	// 9..11 (no type 12), Ingredients 13; -1 none. A row with several types takes the first in popup order.
	inline int AlchemyRank(std::uint32_t a_tags)
	{
		using itemtags::Alchemy;
		using itemtags::Index;
		const auto has = [&](Alchemy a_choice) { return itemtags::Includes(Index(a_choice), a_tags); };
		const auto first = [&](Alchemy a_from, Alchemy a_to) {
			for (unsigned id = Index(a_from); id <= Index(a_to); ++id)
				if (itemtags::Includes(id, a_tags)) return static_cast<int>(id - Index(a_from));
			return -1;
		};
		if (has(Alchemy::Tools)) return 0;
		if (has(Alchemy::Potions)) { const int t = first(Alchemy::Restore, Alchemy::Other); return t < 0 ? 8 : 1 + t; }
		if (has(Alchemy::Poisons)) { const int t = first(Alchemy::Damage, Alchemy::Disable); return t < 0 ? 12 : 9 + t; }
		if (has(Alchemy::Ingredients)) return 13;
		return -1;
	}

	inline int GroupRank(itemtags::Tab a_tab, std::uint32_t a_tags)
	{
		if (a_tab == itemtags::Tab::Potions) return AlchemyRank(a_tags);
		if (a_tab != itemtags::Tab::AllItems) return FirstChoice(a_tab, a_tags);
		const unsigned category = itemtags::CategoryOf(a_tags);
		if (category < 1 || category > 4) return -1;
		const auto tab = static_cast<itemtags::Tab>(category - 1);
		const int sub = tab == itemtags::Tab::Potions ? AlchemyRank(a_tags) : FirstChoice(tab, a_tags);
		return static_cast<int>(category) * 32 + (sub < 0 ? 31 : sub);
	}

	// A spell row's group for the Magic filter of picker kind a_group (RMI-051, owner 2026-11-07): 5 Type sort = type
	// (spells, scrolls, powers) then school; 1 School sort = school then type; 2 Effect sort = type then effect family.
	// A missing level sorts after the known ones of that level; rows with neither level known (e.g. kPassAll
	// without a type tag) have none.
	inline int MagicGroupRank(int a_group, std::uint64_t a_mask)
	{
		// An unresolved row (kPassAll) keeps its type tag (e.g. a scroll), but its school and effect are unknown.
		const bool unresolved = (a_mask & magicrows::kPassAll) != 0;
		const int type = (a_mask & magicrows::kSpellTag) ? 0 : (a_mask & magicrows::kScrollTag) ? 1 : (a_mask & magicrows::kPowerTag) ? 2 : -1;
		const int school = unresolved ? -1 : FirstChoice(itemtags::Tab::MagicSchool, static_cast<std::uint32_t>(a_mask));
		const int effect = unresolved ? -1 : FirstChoice(itemtags::Tab::MagicEffect, static_cast<std::uint32_t>(a_mask >> 32));
		const auto pair = [](int a_first, int a_second) {
			if (a_first < 0 && a_second < 0) return -1;
			return (a_first < 0 ? 63 : a_first) * 64 + (a_second < 0 ? 63 : a_second);
		};
		switch (a_group) {
		case 5:  return pair(type, school);
		case 1:  return pair(school, type);
		case 2:  return pair(type, effect);
		default: return -1;
		}
	}

	// Picker command ids for a filter's category sort (sort commands use -100..-159, picker commands -1..-6).
	constexpr int kGroupCommandBase = -200;
	inline int    GroupCommand(Action a_action) { return kGroupCommandBase - static_cast<int>(a_action); }
	inline bool   IsGroupCommand(int a_cmd) { return a_cmd <= kGroupCommandBase && a_cmd > kGroupCommandBase - 3; }
	inline Action GroupCommandAction(int a_cmd) { return static_cast<Action>(kGroupCommandBase - a_cmd); }
}
