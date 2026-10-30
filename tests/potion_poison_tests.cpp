#include "../src/sorting/rules/PickerNavigation.h"

#include <cstdio>
#include <initializer_list>
#include <string_view>

// RMI-014 and the owner's potion regrouping (2026-10-30). Written with plain indices so it
// also compiles against the old Alchemy layout.
// Alchemy ids: All0 Potions1 Poisons2 Ingredients3 Tools4 | Restore5 Fortify6 Ward7 Stealth8
// Utility9 Cures10 Other11 | Damage12 Elemental13 Disable14. Applied bit = 1 << (id - 1).
// A poison (poison spell type, or every effect hostile) never carries a potion group.
int main()
{
	using namespace itemtags;
	int checks = 0, failures = 0;
	const auto check = [&](bool ok, const char* label) {
		++checks;
		if (!ok) { ++failures; std::fprintf(stderr, "FAIL %s\n", label); }
	};
	const auto bit = [](unsigned id) { return 1u << (id - 1); };
	const auto names = [](Tab tab, std::initializer_list<const char*> want) {
		const auto& info = Info(tab);
		if (info.count != want.size()) return false;
		unsigned i = 0;
		for (auto name : want) if (std::string_view(info.names[i++]) != name) return false;
		return true;
	};
	const auto icons = [](Tab tab, std::initializer_list<const wchar_t*> want) {
		const auto& info = Info(tab);
		if (info.count != want.size()) return false;
		unsigned i = 0;
		for (auto icon : want) if (std::wstring_view(info.icons[i++]) != icon) return false;
		return true;
	};
	const auto tags1 = [](std::uint32_t code, int hostile) { return PotionTags(&code, 1, hostile); };
	const auto Potion = bit(1), Poisons = bit(2);

	// Menus.
	check(names(Tab::Potions, { "All", "Potions", "Poisons", "Ingredients", "Tools" }), "Alchemy root has Poisons after Potions");
	check(names(Tab::PotionEffects, { "All potions", "Restore", "Fortify", "Ward", "Stealth", "Utility", "Cures", "Other" }), "potion groups");
	check(icons(Tab::PotionEffects, { L"Potion-All.png", L"Potion-Restore.png", L"Potion-Fortify.png", L"Potion-Ward.png",
		L"Potion-Stealth.png", L"Potion-Utility.png", L"Potion-Cure.png", L"Potion-Other.png" }), "potion groups have their own files");
	const bool poisonMenu = Info(Tab::Potions).count == 5;
	const auto PoisonTab = static_cast<Tab>(9);
	check(poisonMenu && names(PoisonTab, { "All poisons", "Damage", "Elemental", "Disable" }), "poison groups");
	check(poisonMenu && icons(PoisonTab, { L"Poison-All.png", L"Poison-Damage.png", L"Poison-Elemental.png", L"Poison-Disable.png" }), "poison groups have their own files");
	check(poisonMenu && std::wstring_view(Info(Tab::Potions).icons[2]) == L"Alchemy-Poisons.png", "Poisons parent has its own file");
	check(AlchemyTags(25) == bit(3) && AlchemyTags(19) == bit(4), "ingredients and tools keep their own ids");

	// Potion groups (every master effect code, see docs/inventory/alchemy-misc).
	struct Case { std::uint32_t code; unsigned id; const char* label; };
	const Case potions[]{
		{ Code('R','E','H','E'), 5, "Restore Health" }, { Code('R','E','S','P'), 5, "Restore Magicka" },
		{ Code('R','E','F','A'), 5, "Restore Fatigue" }, { Code('R','E','A','T'), 5, "Restore Attribute" },
		{ Code('F','O','H','E'), 6, "Fortify Health" }, { Code('F','O','S','P'), 6, "Fortify Magicka" },
		{ Code('F','O','F','A'), 6, "Fortify Fatigue" }, { Code('F','O','A','T'), 6, "Fortify Attribute" },
		{ Code('F','O','S','K'), 6, "Fortify Skill" }, { Code('F','O','M','M'), 6, "Fortify Magicka Multiplier" },
		{ Code('R','S','F','I'), 7, "Resist Fire" }, { Code('R','S','P','O'), 7, "Resist Poison" }, { Code('R','S','M','A'), 7, "Resist Magic" },
		{ Code('S','H','L','D'), 7, "Shield" }, { Code('F','I','S','H'), 7, "Fire Shield" }, { Code('L','I','S','H'), 7, "Shock Shield" },
		{ Code('R','E','D','G'), 7, "Reflect Damage" }, { Code('R','F','L','C'), 7, "Reflect Spell" }, { Code('S','A','B','S'), 7, "Spell Absorption" },
		{ Code('C','H','M','L'), 8, "Chameleon" }, { Code('I','N','V','I'), 8, "Invisibility" },
		{ Code('N','E','Y','E'), 9, "Night-Eye" }, { Code('L','G','H','T'), 9, "Light" }, { Code('D','T','C','T'), 9, "Detect Life" },
		{ Code('F','T','H','R'), 9, "Feather" }, { Code('W','A','B','R'), 9, "Water Breathing" }, { Code('W','A','W','A'), 9, "Water Walking" },
		{ Code('C','U','D','I'), 10, "Cure Disease" }, { Code('C','U','P','O'), 10, "Cure Poison" }, { Code('C','U','P','A'), 10, "Cure Paralysis" },
		{ Code('D','S','P','L'), 10, "Dispel" },
		{ Code('S','E','F','F'), 11, "script-only quest potion is Other" },
	};
	for (const auto& c : potions) check(tags1(c.code, 0) == (Potion | bit(c.id)), c.label);
	check(PotionTags(nullptr, 0, 0) == (Potion | bit(11)), "unreadable/empty potion is Other");
	const std::uint32_t skooma[]{ Code('R','E','A','T'), Code('D','G','A','T') };
	check(PotionTags(skooma, 2, 1) == (Potion | bit(5)), "mixed potion keeps only its positive groups");
	const std::uint32_t mixed[]{ Code('R','E','H','E'), Code('F','O','S','P'), Code('I','N','V','I') };
	check(PotionTags(mixed, 3, 0) == (Potion | bit(5) | bit(6) | bit(8)), "several effects: every matching group, no Other");

	// Poison groups.
	const Case poisons[]{
		{ Code('D','G','H','E'), 12, "Damage Health" }, { Code('D','G','S','P'), 12, "Damage Magicka" },
		{ Code('D','G','F','A'), 12, "Damage Fatigue" }, { Code('D','G','A','T'), 12, "Damage Attribute" },
		{ Code('D','R','H','E'), 12, "Drain Health" }, { Code('D','R','S','P'), 12, "Drain Magicka" },
		{ Code('D','R','F','A'), 12, "Drain Fatigue" }, { Code('D','R','A','T'), 12, "Drain Attribute" },
		{ Code('F','I','D','G'), 13, "Fire Damage" }, { Code('F','R','D','G'), 13, "Frost Damage" },
		{ Code('S','H','D','G'), 13, "Shock Damage" }, { Code('W','K','F','I'), 13, "Weakness to Fire" },
		{ Code('P','A','R','A'), 14, "Paralyze" }, { Code('S','L','N','C'), 14, "Silence" }, { Code('B','R','D','N'), 14, "Burden" },
	};
	for (const auto& c : poisons) check(tags1(c.code, 1) == (Poisons | bit(c.id)), c.label);
	const std::uint32_t nab[]{ Code('F','I','D','G'), Code('F','R','D','G'), Code('S','H','D','G') };
	check(PotionTags(nab, 3, 3) == (Poisons | bit(13)), "three elemental effects: one Elemental group");
	check(tags1(Code('S','E','F','F'), 1) == Poisons, "script-only poison stays in All poisons");
	check(PotionTags(&potions[0].code, 1, 0, true) == Poisons, "native poison type never gets potion groups");
	check(!Includes(1, tags1(Code('D','G','H','E'), 1)) && !Includes(2, tags1(Code('R','E','H','E'), 0)), "potions and poisons stay apart");

	// Navigation: Poisons opens its own submenu; leaves map to flat ids; Back returns to Poisons.
	auto next = pickernav::Choose(Tab::Potions, Tab::Potions, 0, 4, 2);
	check(poisonMenu && next.view == PoisonTab && !next.close && next.choice == 4, "enter Poisons without applying");
	if (poisonMenu) {
		next = pickernav::Back(Tab::Potions, PoisonTab, 0, 4);
		check(next.view == Tab::Potions && next.cursor == 2 && !next.close && next.choice == 4, "Back from poison groups returns to Poisons");
		for (unsigned i = 0; i < 4; ++i) {
			const unsigned id = i == 0 ? 2u : 11u + i;
			next = pickernav::Choose(Tab::Potions, PoisonTab, 0, 4, i);
			check(next.close && next.choice == id && next.category == 0, "poison leaf maps to flat id");
			check(pickernav::Highlight(Tab::Potions, PoisonTab, 0, id) == i, "poison highlight round-trip");
			check(pickernav::Highlight(Tab::Potions, Tab::Potions, 0, id) == 2, "root highlights Poisons");
			check(std::wstring_view(SelectionIcon(Tab::Potions, id)) == Info(PoisonTab).icons[i], "header shows the poison group");
			next = pickernav::Choose(Tab::AllItems, PoisonTab, 1, 9, i);
			check(next.close && next.category == 3 && next.choice == id, "All Items poison leaf applies Alchemy category");
			check(pickernav::Highlight(Tab::AllItems, PoisonTab, 3, id) == i, "All Items poison highlight");
		}
		next = pickernav::Back(Tab::AllItems, PoisonTab, 1, 9);
		check(next.view == Tab::Potions && next.cursor == 2 && !next.close, "All Items Back from poisons");
	}
	for (unsigned i = 0; i < 8; ++i) {
		const unsigned id = i == 0 ? 1u : 4u + i;
		next = pickernav::Choose(Tab::Potions, Tab::PotionEffects, 0, 4, i);
		check(next.close && next.choice == id, "potion leaf maps to flat id");
		check(pickernav::Highlight(Tab::Potions, Tab::PotionEffects, 0, id) == i, "potion highlight round-trip");
		check(pickernav::Highlight(Tab::Potions, Tab::Potions, 0, id) == 1, "root highlights Potions");
		check(std::wstring_view(SelectionIcon(Tab::Potions, id)) == Info(Tab::PotionEffects).icons[i], "header shows the potion group");
	}
	for (unsigned leaf : { 0u, 3u, 4u }) {
		next = pickernav::Choose(Tab::Potions, Tab::Potions, 0, 5, leaf);
		check(next.close && next.choice == leaf && pickernav::Highlight(Tab::Potions, Tab::Potions, 0, leaf) == leaf, "All/Ingredients/Tools are leaves");
	}

	std::printf("potion/poison %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
