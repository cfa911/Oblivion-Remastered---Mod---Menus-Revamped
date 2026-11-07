// Category sort on the filters (owner 2026-11-07, KB mods/menus-revamped/evidence/own-sorts-20261107.md
// "Brainstorm: sort by our filters"): new popup = group orders, Misc Scrolls, grouped ordering, one sort at a time.
#include "../src/sorting/rules/ItemSort.h"
#include "../src/sorting/rules/MagicRows.h"
#include "../src/sorting/rules/PickerNavigation.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

int main()
{
	using itemsort::Key;
	using itemsort::Action;
	using itemtags::Tab;
	int checks = 0;
	const auto check = [&](bool ok, const char* label) {
		++checks;
		if (!ok) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); }
	};
	const auto names = [](Tab tab) {
		std::vector<std::string_view> out;
		const auto& info = itemtags::Info(tab);
		for (unsigned i = 0; i < info.count; ++i) out.push_back(info.names[i]);
		return out;
	};
	using V = std::vector<std::string_view>;

	// ---- popup order = group order (owner) ----
	check(names(Tab::Weapons) == V{ "All", "Claymores", "Longswords", "Shortswords", "Daggers", "War axes", "Battleaxes", "Maces",
		"Warhammers", "Staves", "Bows", "Arrows", "Other weapons" }, "weapons order");
	check(names(Tab::Gear) == V{ "All", "Head", "Torso", "Arms", "Legwear", "Feet", "Shield", "Amulet", "Ring" }, "gear order head to toe, then jewellery");
	check(names(Tab::Potions) == V{ "All", "Tools", "Potions", "Poisons", "Ingredients" }, "alchemy order");
	check(names(Tab::Misc) == V{ "All", "Tools", "Soul gems", "Books", "Scrolls", "Keys", "Other" }, "misc order with Scrolls");
	check(std::wstring_view(itemtags::Info(Tab::Misc).icons[4]) == L"MagicType-Scrolls.png", "misc Scrolls icon");
	check(names(Tab::AllItems) == V{ "All", "Weapons", "Gear", "Alchemy", "Misc" }, "all items categories");
	check(names(Tab::MagicSchool) == V{ "All", "Alteration", "Conjuration", "Destruction", "Illusion", "Mysticism", "Restoration" }, "school unchanged");
	check(names(Tab::MagicType) == V{ "All", "Spells", "Scrolls", "Powers" }, "type unchanged");
	check(names(Tab::MagicEffect) == V{ "All", "Fire", "Frost", "Shock", "Damage", "Drain", "Absorb", "Weakness", "Restore & Cure", "Fortify",
		"Shield", "Resistance", "Summon", "Bound Equipment", "Control", "Stealth", "Utility", "Other" }, "effect order: weakness next to the attacks");
	// icons follow the names
	check(std::wstring_view(itemtags::Info(Tab::Gear).icons[1]) == L"Gear-Head.png" && std::wstring_view(itemtags::Info(Tab::Gear).icons[8]) == L"Gear-Ring.png", "gear icons follow");
	check(std::wstring_view(itemtags::Info(Tab::Weapons).icons[1]) == L"Weapon-Claymore.png" && std::wstring_view(itemtags::Info(Tab::Weapons).icons[9]) == L"Weapon-Staff.png", "weapon icons follow");
	check(std::wstring_view(itemtags::Info(Tab::Potions).icons[1]) == L"Alchemy-Tools.png", "alchemy icons follow");
	check(std::wstring_view(itemtags::Info(Tab::MagicEffect).icons[7]) == L"Effect-Weakness.png", "effect icons follow");

	// ---- classification bits follow the new choice ids (bit k = choice k + 1) ----
	check(itemtags::Includes(1, itemtags::GearTags(0x0001)), "head slot = choice 1");
	check(itemtags::Includes(8, itemtags::GearTags(0x0040)), "ring slot = choice 8");
	check(itemtags::Includes(6, itemtags::GearTags(0x2000)), "shield = choice 6");
	check(itemtags::Includes(4, itemtags::WeaponTags(0, "weapons\\iron\\dagger.nif")), "dagger = choice 4");
	check(itemtags::Includes(1, itemtags::WeaponTags(1, "weapons\\iron\\claymore.nif")), "claymore = choice 1");
	check(itemtags::Includes(9, itemtags::WeaponTags(4)), "staff = choice 9");
	check(itemtags::Includes(10, itemtags::WeaponTags(5)), "bow = choice 10");
	check(itemtags::Includes(11, itemtags::AmmoTags(34)), "arrow = choice 11");
	check(itemtags::Includes(12, itemtags::WeaponTags(0, "weapons\\odd\\thing.nif")), "other weapon = choice 12");
	check(itemtags::Includes(1, itemtags::AlchemyTags(19)) && itemtags::Includes(4, itemtags::AlchemyTags(25)), "tools 1, ingredients 4");
	{
		const std::uint32_t restore = itemtags::Code('R', 'E', 'H', 'E');
		const auto potion = itemtags::PotionTags(&restore, 1, 0);
		check(itemtags::Includes(2, potion) && !itemtags::Includes(3, potion), "potion = choice 2");
		const std::uint32_t damage = itemtags::Code('D', 'G', 'H', 'E');
		check(itemtags::Includes(3, itemtags::PotionTags(&damage, 1, 1)), "poison = choice 3");
	}
	check(itemtags::Includes(1, itemtags::MiscTags(27, 0x0A, 0)) && itemtags::Includes(1, itemtags::MiscTags(26, 0x1234, 0)), "lockpick / torch = Tools 1");
	check(itemtags::Includes(2, itemtags::MiscTags(38, 0x1234, 0)), "soul gem 2");
	check(itemtags::Includes(3, itemtags::MiscTags(21, 0x1234, 0)) && !itemtags::Includes(4, itemtags::MiscTags(21, 0x1234, 0)), "plain book = Books 3");
	check(itemtags::Includes(4, itemtags::MiscTags(21, 0x1234, 1)) && !itemtags::Includes(3, itemtags::MiscTags(21, 0x1234, 1)), "scroll flag = Scrolls 4");
	check(itemtags::Includes(5, itemtags::MiscTags(39, 0x1234, 0)), "key 5");
	check(itemtags::Includes(6, itemtags::MiscTags(42, 0x1234, 0)) && itemtags::Includes(6, itemtags::MiscTags(27, 0x1234, 0)), "sigil stone / misc = Other 6");
	{
		const std::uint32_t weak = magiceffects::Code("WKFI");
		check(itemtags::Includes(7, itemtags::EffectTags(&weak, 1)), "weakness to fire = effect choice 7");
		const std::uint32_t fire = magiceffects::Code("FIDG");
		check(itemtags::Includes(1, itemtags::EffectTags(&fire, 1)), "fire damage = effect choice 1");
	}

	// ---- popup navigation with the Alchemy root reordered ----
	{
		using itemtags::Alchemy;
		using itemtags::Index;
		auto r = pickernav::Choose(Tab::Potions, Tab::Potions, 0, 0, Index(Alchemy::Potions));
		check(r.view == Tab::PotionEffects && !r.close, "Potions opens its submenu");
		r = pickernav::Choose(Tab::Potions, Tab::Potions, 0, 0, Index(Alchemy::Poisons));
		check(r.view == Tab::PoisonEffects && !r.close, "Poisons opens its submenu");
		r = pickernav::Choose(Tab::Potions, Tab::Potions, 0, 0, Index(Alchemy::Tools));
		check(r.close && r.choice == Index(Alchemy::Tools), "Tools is a leaf");
		check(pickernav::Back(Tab::Potions, Tab::PotionEffects, 0, 0).cursor == Index(Alchemy::Potions), "back to Potions cell");
		check(pickernav::Back(Tab::Potions, Tab::PoisonEffects, 0, 0).cursor == Index(Alchemy::Poisons), "back to Poisons cell");
		check(pickernav::Highlight(Tab::Potions, Tab::Potions, 0, Index(Alchemy::Restore)) == Index(Alchemy::Potions), "potion group highlights Potions");
	}

	// ---- group ranks ----
	check(itemsort::GroupRank(Tab::Gear, itemtags::GearTags(0x0004)) < itemsort::GroupRank(Tab::Gear, itemtags::GearTags(0x0040)), "torso before ring");
	check(itemsort::GroupRank(Tab::Gear, itemtags::GearTags(0x0005)) == itemsort::GroupRank(Tab::Gear, itemtags::GearTags(0x0001)), "several slots: the first in order");
	check(itemsort::GroupRank(Tab::Gear, 0) < 0, "unclassified: no group");
	check(itemsort::GroupRank(Tab::Weapons, itemtags::WeaponTags(1, "claymore.nif")) < itemsort::GroupRank(Tab::Weapons, itemtags::WeaponTags(0, "dagger.nif")), "claymore before dagger");
	{
		const auto cat = [](unsigned c, std::uint32_t tags) { return (tags & itemtags::kTagBits) | (c << 28); };
		const int weapon = itemsort::GroupRank(Tab::AllItems, cat(1, itemtags::AmmoTags(34)));
		const int head = itemsort::GroupRank(Tab::AllItems, cat(2, itemtags::GearTags(0x0001)));
		const int ring = itemsort::GroupRank(Tab::AllItems, cat(2, itemtags::GearTags(0x0040)));
		const int tool = itemsort::GroupRank(Tab::AllItems, cat(3, itemtags::AlchemyTags(19)));
		const int key = itemsort::GroupRank(Tab::AllItems, cat(4, itemtags::MiscTags(39, 1, 0)));
		check(weapon >= 0 && weapon < head && head < ring && ring < tool && tool < key, "all items: category, then its tab order");
		check(itemsort::GroupRank(Tab::AllItems, cat(0, 0)) < 0, "all items: no category, no group");
		check(itemsort::GroupRank(Tab::AllItems, cat(2, 0)) > ring, "all items: unclassified gear after the gear groups");
	}
	// Magic: group = the picker kind (1 school, 2 effect, 5 type); kPassAll rows have no group.
	{
		const std::uint64_t alteration = itemtags::SchoolTags(0, 0), restoration = itemtags::SchoolTags(5, 0);
		check(itemsort::MagicGroupRank(1, alteration | magicrows::kSpellTag) < itemsort::MagicGroupRank(1, restoration | magicrows::kSpellTag), "school A-Z");
		check(itemsort::MagicGroupRank(5, magicrows::kSpellTag) < itemsort::MagicGroupRank(5, magicrows::kScrollTag) &&
			itemsort::MagicGroupRank(5, magicrows::kScrollTag) < itemsort::MagicGroupRank(5, magicrows::kPowerTag), "type: spells, scrolls, powers");
		const std::uint32_t fire = magiceffects::Code("FIDG"), weak = magiceffects::Code("WKFI"), heal = magiceffects::Code("REHE");
		const auto effect = [](std::uint32_t code) { return static_cast<std::uint64_t>(itemtags::EffectTags(&code, 1)) << 32; };
		check(itemsort::MagicGroupRank(2, effect(fire)) < itemsort::MagicGroupRank(2, effect(weak)) &&
			itemsort::MagicGroupRank(2, effect(weak)) < itemsort::MagicGroupRank(2, effect(heal)), "effect order");
		check(itemsort::MagicGroupRank(1, magicrows::kPassAll) < 0 && itemsort::MagicGroupRank(2, magicrows::kPassAll) < 0, "unresolved row: no group");
	}

	// ---- one sort at a time: the category sort and the sort buttons exclude each other ----
	{
		const itemsort::State grouped = itemsort::ApplyGroup({}, 0, Action::Click);
		check(grouped.group == 0 && !grouped.descending, "filter click: grouped ascending");
		check(itemsort::ApplyGroup(grouped, 0, Action::Click).descending, "second click reverses");
		check(!itemsort::ApplyGroup(itemsort::ApplyGroup(grouped, 0, Action::Click), 0, Action::Click).descending, "third click back");
		check(itemsort::ApplyGroup(grouped, 0, Action::Descending).descending && !itemsort::ApplyGroup(grouped, 0, Action::Ascending).descending, "stick up/down");
		const auto effect = itemsort::ApplyGroup(itemsort::ApplyGroup({}, 1, Action::Descending), 2, Action::Click);
		check(effect.group == 2 && !effect.descending, "another filter starts ascending");
		const auto keys = itemsort::Keys(Tab::Gear);
		const auto name = itemsort::Apply(grouped, keys, Key::Name, Action::Click);
		check(name.group < 0 && name.key == Key::Name && !name.descending, "A-Z button switches the category sort off (A-Z ascending, not reversed)");
		const auto weight = itemsort::Apply(grouped, keys, Key::Weight, Action::Click);
		check(weight.group < 0 && weight.key == Key::Weight && weight.descending, "a sort button switches it off");
		check(itemsort::Apply(grouped, Tab::Gear, Key::Weight, Action::Click).group < 0, "tab Apply too");
		check(itemsort::ApplyGroup(weight, 0, Action::Click).group == 0, "a filter click switches the button sort off");
		check(itemsort::Effective(grouped, keys).group == 0 && itemsort::Effective(grouped, Tab::Misc).group == 0, "a grouped state stays effective");
		check(itemsort::Default(true, 0).group == 0 && itemsort::Default(false, 0).group < 0 && itemsort::Default(true, 1).group == 1, "INI default");
		check(itemsort::Default(false, 0) == itemsort::State{}, "INI off = A-Z");
	}

	// ---- grouped order: by group, A-Z inside, unknown last in both directions ----
	{
		std::vector<itemsort::Row> rows(5);
		rows[0].name = "Zeta ring";   rows[0].group = 8;
		rows[1].name = "Iron helmet"; rows[1].group = 1;
		rows[2].name = "Amber ring";  rows[2].group = 8;
		rows[3].name = "Mystery";     rows[3].group = -1;
		rows[4].name = "Cuirass";     rows[4].group = 2;
		check(itemsort::Order(rows, itemsort::ApplyGroup({}, 0, Action::Ascending)) == std::vector{ 1, 4, 2, 0, 3 }, "grouped ascending");
		check(itemsort::Order(rows, itemsort::ApplyGroup({}, 0, Action::Descending)) == std::vector{ 0, 2, 4, 1, 3 }, "grouped reversed: groups reversed, Z-A inside (RMI-049), unknown last");
	}

	// ---- commands ----
	check(itemsort::IsGroupCommand(itemsort::GroupCommand(Action::Click)) && itemsort::IsGroupCommand(itemsort::GroupCommand(Action::Descending)), "group commands");
	check(itemsort::GroupCommandAction(itemsort::GroupCommand(Action::Ascending)) == Action::Ascending, "group command action");
	check(!itemsort::IsSortCommand(itemsort::GroupCommand(Action::Click)) && !itemsort::IsGroupCommand(itemsort::SortCommand(Key::Name)), "no clash with sort commands");
	check(!itemsort::IsGroupCommand(-1) && !itemsort::IsGroupCommand(-4) && !itemsort::IsGroupCommand(0), "no clash with picker commands");

	// RMI-051 (owner 2026-11-07): Alchemy groups follow the popup tree: Tools, Potions by Potion type (Restore, Fortify,
	// Ward, Stealth, Utility, Cures, Other), Poisons by Poison type (Damage, Elemental, Disable), Ingredients.
	{
		constexpr auto code = [](const char (&c)[5]) { return std::uint32_t(c[0]) | std::uint32_t(c[1]) << 8 | std::uint32_t(c[2]) << 16 | std::uint32_t(c[3]) << 24; };
		const std::uint32_t rehe[] = { code("REHE") }, fohe[] = { code("FOHE") }, rsfi[] = { code("RSFI") }, chml[] = { code("CHML") },
			neye[] = { code("NEYE") }, cudi[] = { code("CUDI") }, sabs[] = { code("SABS") }, dghe[] = { code("DGHE") }, fidg[] = { code("FIDG") },
			slnc[] = { code("SLNC") }, both[] = { code("FOHE"), code("REHE") };
		const auto rank = [](std::uint32_t tags) { return itemsort::GroupRank(Tab::Potions, tags); };
		const int tools = rank(itemtags::AlchemyTags(19)), restore = rank(itemtags::PotionTags(rehe, 1, 0)), fortify = rank(itemtags::PotionTags(fohe, 1, 0)),
			ward = rank(itemtags::PotionTags(rsfi, 1, 0)), stealth = rank(itemtags::PotionTags(chml, 1, 0)), utility = rank(itemtags::PotionTags(neye, 1, 0)),
			cures = rank(itemtags::PotionTags(cudi, 1, 0)), other = rank(itemtags::PotionTags(nullptr, 0, 0)), damage = rank(itemtags::PotionTags(dghe, 1, 1)),
			elemental = rank(itemtags::PotionTags(fidg, 1, 1)), disable = rank(itemtags::PotionTags(slnc, 1, 1)), ingredients = rank(itemtags::AlchemyTags(25));
		(void)sabs;
		check(tools >= 0 && tools < restore, "alchemy: tools first");
		check(restore < fortify && fortify < ward && ward < stealth && stealth < utility && utility < cures && cures < other, "potions by potion type, popup order");
		check(other < damage && damage < elemental && elemental < disable, "poisons after potions, by poison type");
		check(disable < ingredients, "ingredients last");
		check(rank(itemtags::PotionTags(both, 2, 0)) == restore, "several types: the first in popup order");
		const auto cat = [](unsigned c, std::uint32_t tags) { return (tags & itemtags::kTagBits) | (c << 28); };
		const auto all = [&](unsigned c, std::uint32_t tags) { return itemsort::GroupRank(Tab::AllItems, cat(c, tags)); };
		const int allRestore = all(3, itemtags::PotionTags(rehe, 1, 0)), allFortify = all(3, itemtags::PotionTags(fohe, 1, 0)),
			allDisable = all(3, itemtags::PotionTags(slnc, 1, 1)), allIngredient = all(3, itemtags::AlchemyTags(25));
		check(all(2, itemtags::GearTags(0x0040)) < all(3, itemtags::AlchemyTags(19)) && allRestore < allFortify && allFortify < allDisable &&
			allDisable < allIngredient && allIngredient < all(4, itemtags::MiscTags(39, 1, 0)) && all(3, 0) > allIngredient && all(3, 0) < all(4, itemtags::MiscTags(39, 1, 0)),
			"all items: alchemy sub-groups inside the alchemy category");
	}

	// RMI-051 Magic (owner 2026-11-07): Type sort = type -> school; School sort = school -> type; Effect sort = type ->
	// effect; A-Z inside (rows carry no value).
	{
		using magicrows::kSpellTag, magicrows::kScrollTag, magicrows::kPowerTag;
		const std::uint64_t alt = itemtags::SchoolTags(0, 0), rest = itemtags::SchoolTags(5, 0);
		const std::uint32_t fire = magiceffects::Code("FIDG"), heal = magiceffects::Code("REHE");
		const auto eff = [](std::uint32_t code) { return static_cast<std::uint64_t>(itemtags::EffectTags(&code, 1)) << 32; };
		const auto r = [](int g, std::uint64_t m) { return itemsort::MagicGroupRank(g, m); };
		check(r(5, kSpellTag | alt) < r(5, kSpellTag | rest) && r(5, kSpellTag | rest) < r(5, kScrollTag | alt) &&
			r(5, kScrollTag | rest) < r(5, kPowerTag | alt), "type sort: type, then school");
		check(r(1, alt | kSpellTag) < r(1, alt | kPowerTag) && r(1, alt | kPowerTag) < r(1, rest | kSpellTag), "school sort: school, then type");
		check(r(2, kSpellTag | eff(fire)) < r(2, kSpellTag | eff(heal)) && r(2, kSpellTag | eff(heal)) < r(2, kPowerTag | eff(fire)),
			"effect sort: type, then effect");
		check(r(5, magicrows::kPassAll | kScrollTag) > r(5, kSpellTag | rest) && r(5, magicrows::kPassAll | kScrollTag) < r(5, kPowerTag | alt),
			"unresolved scroll keeps its type group");
	}

	std::printf("category_sort_tests: %d checks passed\n", checks);
	return 0;
}
