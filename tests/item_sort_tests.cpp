// Own sort bars (owner 2026-11-07): per-tab sort sets, click/reverse rule, ordering with missing values last.
#include "../src/sorting/rules/ItemSort.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

int main()
{
	using itemsort::Key;
	using itemtags::Tab;
	int checks = 0;
	const auto check = [&](bool ok, const char* label) {
		++checks;
		if (!ok) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); }
	};

	// Inventory sort sets from the owner's 1.4.1 screenshots, left to right.
	check(itemsort::Keys(Tab::Weapons) == std::vector{ Key::Name, Key::Health, Key::Damage, Key::Weight, Key::Value }, "weapons set");
	check(itemsort::Keys(Tab::Gear) == std::vector{ Key::Name, Key::Health, Key::Armor, Key::Weight, Key::Value }, "gear set");
	check(itemsort::Keys(Tab::Potions) == std::vector{ Key::Name, Key::Weight, Key::Value }, "alchemy set");
	check(itemsort::Keys(Tab::Misc) == std::vector{ Key::Name, Key::Weight, Key::Value }, "misc set");
	check(itemsort::Keys(Tab::AllItems) == std::vector{ Key::Name, Key::Health, Key::Damage, Key::Armor, Key::Weight, Key::Value }, "all items set");

	// First click: A-Z ascending, numbers highest first; same key again reverses.
	check(!itemsort::DefaultDescending(Key::Name), "name ascending first");
	for (Key k : { Key::Health, Key::Damage, Key::Armor, Key::Weight, Key::Value }) check(itemsort::DefaultDescending(k), "numbers descending first");
	auto s = itemsort::Click({}, Key::Weight);
	check(s.key == Key::Weight && s.descending, "click weight");
	s = itemsort::Click(s, Key::Weight);
	check(s.key == Key::Weight && !s.descending, "click weight again reverses");
	s = itemsort::Click(s, Key::Name);
	check(s.key == Key::Name && !s.descending, "other key starts in its default");
	s = itemsort::Click(s, Key::Name);
	check(s.key == Key::Name && s.descending, "name reversed");

	// A remembered key the tab does not offer falls back to A-Z ascending.
	const itemsort::State armor{ Key::Armor, true };
	check(itemsort::Effective(armor, Tab::Gear).key == Key::Armor, "gear keeps armor");
	const auto fallback = itemsort::Effective(armor, Tab::Misc);
	check(fallback.key == Key::Name && !fallback.descending, "misc has no armor");

	// Ordering.
	const std::vector<itemsort::Row> rows{
		{ "iron Sword", 10, 16, 9, -1, 80 },     // 0
		{ "Apple", 1, 0.5f, -1, -1, -1 },        // 1
		{ "Bow", 30, 12, 11, -1, 100 },          // 2
		{ "apple", 1, 0.5f, -1, -1, -1 },        // 3 (tie with 1 by name, case-insensitive)
		{ "Cuirass", 50, 30, -1, 12, 60 },       // 4
	};
	const auto order = [&](Key k, bool d) { return itemsort::Order(rows, { k, d }); };
	check(order(Key::Name, false) == std::vector{ 1, 3, 2, 4, 0 }, "name ascending, case-insensitive, stable");
	check(order(Key::Name, true) == std::vector{ 0, 4, 2, 1, 3 }, "name descending keeps original order on ties");
	check(order(Key::Value, true) == std::vector{ 4, 2, 0, 1, 3 }, "value highest first");
	check(order(Key::Value, false) == std::vector{ 1, 3, 0, 2, 4 }, "value lowest first");
	check(order(Key::Weight, true) == std::vector{ 4, 0, 2, 1, 3 }, "weight highest first");
	check(order(Key::Health, true) == std::vector{ 2, 0, 4, 1, 3 }, "health, missing last");
	check(order(Key::Health, false) == std::vector{ 4, 0, 2, 1, 3 }, "health ascending, missing still last");
	check(order(Key::Damage, true) == std::vector{ 2, 0, 1, 3, 4 }, "damage, missing last by name");
	check(order(Key::Armor, false) == std::vector{ 4, 1, 3, 2, 0 }, "armor, missing last by name");
	check(itemsort::Order({}, {}).empty(), "empty list");

	// Native wrappers to collapse; commands never collide with picker commands (-1..-6, choices >= 0).
	check(itemsort::IsNativeSortWrapper("Name") && itemsort::IsNativeSortWrapper("Price") && itemsort::IsNativeSortWrapper("ItemCharge"), "native wrappers");
	check(!itemsort::IsNativeSortWrapper("SizeBox_0") && !itemsort::IsNativeSortWrapper("School"), "not native sort wrappers");
	for (unsigned k = 0; k < static_cast<unsigned>(Key::kCount); ++k) {
		const int cmd = itemsort::SortCommand(static_cast<Key>(k));
		check(cmd < -6 && itemsort::IsSortCommand(cmd) && itemsort::SortCommandKey(cmd) == static_cast<Key>(k), "sort command round trip");
	}
	check(!itemsort::IsSortCommand(-1) && !itemsort::IsSortCommand(-6) && !itemsort::IsSortCommand(0), "picker commands are not sort commands");
	using itemsort::Action;
	for (Action a : { Action::Click, Action::Ascending, Action::Descending })
		for (unsigned k = 0; k < static_cast<unsigned>(Key::kCount); ++k) {
			const int cmd = itemsort::SortCommand(static_cast<Key>(k), a);
			check(itemsort::IsSortCommand(cmd) && itemsort::SortCommandKey(cmd) == static_cast<Key>(k) && itemsort::SortCommandAction(cmd) == a, "action round trip");
		}
	check(!itemsort::IsSortCommand(itemsort::kSortCommandBase - static_cast<int>(Key::kCount)) && !itemsort::IsSortCommand(itemsort::kSortCommandBase - 3 * itemsort::kActionStride), "out of range");
	// Apply: Up/Down set the direction of the focused sort; a click on a sort the tab lacks starts from A-Z.
	check(itemsort::Apply({ Key::Value, true }, Tab::Misc, Key::Value, Action::Ascending) == itemsort::State{ Key::Value, false }, "up = ascending");
	check(itemsort::Apply({ Key::Name, false }, Tab::Misc, Key::Weight, Action::Descending) == itemsort::State{ Key::Weight, true }, "down = descending");
	check(itemsort::Apply({ Key::Armor, false }, Tab::Misc, Key::Name, Action::Click) == itemsort::State{ Key::Name, true }, "click on A-Z after a fallback reverses A-Z");

	// Magic spell pages (owner screenshots; native enabled set [0 Name, 2, 4, 6 Cost]; ItemCategory/School are our pickers).
	const std::vector<Key> magic = itemsort::MagicKeys();
	check(magic == std::vector{ Key::Name, Key::Cost }, "magic spell pages: A-Z and Cost");
	check(itemsort::DefaultDescending(Key::Cost), "cost: highest first like other numbers");
	check(itemsort::Effective({ Key::Weight, true }, magic) == itemsort::State{}, "a key the page lacks falls back to A-Z");
	check(itemsort::Effective({ Key::Cost, false }, magic) == itemsort::State{ Key::Cost, false }, "cost kept");
	check(itemsort::Apply({ Key::Cost, true }, magic, Key::Cost, itemsort::Action::Click) == itemsort::State{ Key::Cost, false }, "click reverses cost");
	check(itemsort::MagicNativeKey("Name") == 0 && itemsort::MagicNativeKey("Cost") == static_cast<int>(Key::Cost), "magic native wrappers replaced in place");
	check(itemsort::MagicNativeKey("School") < 0 && itemsort::MagicNativeKey("ItemCategory") < 0 && itemsort::MagicNativeKey("EffectValue") < 0, "pickers and Active Effects sorts untouched");
	check(itemsort::NativeSortType(Key::Name) == 0 && itemsort::NativeSortType(Key::Cost) == 6, "magic sort enum values used to republish");
	{
		std::vector<itemsort::Row> spells(3);
		spells[0].name = "Fire"; spells[0].cost = 20;
		spells[1].name = "Absorb"; spells[1].cost = 50;
		spells[2].name = "Calm"; spells[2].cost = 5;
		check(itemsort::Order(spells, { Key::Cost, true }) == std::vector{ 1, 0, 2 }, "cost highest first");
		check(itemsort::Order(spells, { Key::Name, false }) == std::vector{ 1, 2, 0 }, "spells by name");
	}

	// Magic Active Effects (owner screenshot: A-Z, good/bad masks, category star, school scroll, magnitude).
	const std::vector<Key> active = itemsort::ActiveKeys();
	check(active == std::vector{ Key::Name, Key::EffectValue, Key::Category, Key::School, Key::Magnitude }, "active effects set and order");
	check(!itemsort::DefaultDescending(Key::EffectValue) && !itemsort::DefaultDescending(Key::Category) && !itemsort::DefaultDescending(Key::School),
		"categorical sorts start ascending");
	check(itemsort::DefaultDescending(Key::Magnitude), "magnitude: highest first");
	check(itemsort::NativeSortType(Key::Category) == 2, "category = 2, used to republish on page 4");
	check(itemsort::ActiveNativeKey("Name") == 0 && itemsort::ActiveNativeKey("EffectValue") == static_cast<int>(Key::EffectValue) &&
		itemsort::ActiveNativeKey("ItemCategory") == static_cast<int>(Key::Category) && itemsort::ActiveNativeKey("School") == static_cast<int>(Key::School) &&
		itemsort::ActiveNativeKey("Magnitude") == static_cast<int>(Key::Magnitude), "active effects native wrappers");
	check(itemsort::ActiveNativeKey("Cost") < 0 && itemsort::ActiveNativeKey("Price") < 0, "not an active effects wrapper");
	{
		std::vector<itemsort::Row> effects(3);
		effects[0].name = "Fortify"; effects[0].magnitude = 10; effects[0].school = 5; effects[0].category = 18; effects[0].effectValue = 1;
		effects[1].name = "Burden";  effects[1].magnitude = 50; effects[1].school = 0; effects[1].category = 20; effects[1].effectValue = 2;
		effects[2].name = "Absorb";  effects[2].magnitude = 5;  effects[2].school = 0; effects[2].category = 18; effects[2].effectValue = 1;
		check(itemsort::Order(effects, { Key::Magnitude, true }) == std::vector{ 1, 0, 2 }, "magnitude highest first");
		check(itemsort::Order(effects, { Key::School, false }) == std::vector{ 2, 1, 0 }, "school ascending, ties by name");
		check(itemsort::Order(effects, { Key::Category, false }) == std::vector{ 2, 0, 1 }, "category ascending, ties by name");
		check(itemsort::Order(effects, { Key::EffectValue, true }) == std::vector{ 1, 2, 0 }, "effect value descending, ties by name");
	}

	std::printf("item_sort_tests: %d checks passed\n", checks);
	return 0;
}
