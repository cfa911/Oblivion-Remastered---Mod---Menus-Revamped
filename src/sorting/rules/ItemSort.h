#pragma once

// Own sort bars replacing the native sorts (owner 2026-11-07, KB mods/menus-revamped/evidence/own-sorts-20261107.md).
// Pure rules, no engine types: unit-tested in tests/item_sort_tests.cpp.

#include "sorting/rules/ItemTags.h"

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

		bool operator==(const State&) const = default;
	};

	// One list row's sortable values (OriginalInventoryMenuItemProperties); a negative number = none (sorts last).
	struct Row
	{
		std::string name;
		float       price = -1, weight = -1, damage = -1, armor = -1, health = -1, cost = -1;
		float       effectValue = -1, category = -1, school = -1, magnitude = -1;   // Magic Active Effects
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
		return Offers(a_tab, a_state.key) ? a_state : State{};
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

	// The display order: a permutation of the row indices. Missing numbers last in both directions;
	// ties by name ascending, then the original order.
	inline std::vector<int> Order(std::span<const Row> a_rows, State a_state)
	{
		std::vector<int>         order(a_rows.size());
		std::vector<std::string> names(a_rows.size());
		for (int i = 0; i < static_cast<int>(order.size()); ++i) {
			order[i] = i;
			names[i] = FoldName(a_rows[i].name);
		}
		const bool d = a_state.descending;
		std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
			if (a_state.key == Key::Name) return names[a] != names[b] && ((names[a] < names[b]) != d);
			const float x = Number(a_rows[a], a_state.key), y = Number(a_rows[b], a_state.key);
			if ((x < 0) != (y < 0)) return y < 0;
			if (x >= 0 && x != y) return (x < y) != d;
			return names[a] < names[b];
		});
		return order;
	}

	// The state after a command on a tab (the remembered state first falls back to what the tab offers).
	inline State Apply(State a_state, itemtags::Tab a_tab, Key a_key, Action a_action)
	{
		const auto current = Effective(a_state, a_tab);
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
		return std::find(a_keys.begin(), a_keys.end(), a_state.key) != a_keys.end() ? a_state : State{};
	}

	inline State Apply(State a_state, std::span<const Key> a_keys, Key a_key, Action a_action)
	{
		const auto current = Effective(a_state, a_keys);
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
}
