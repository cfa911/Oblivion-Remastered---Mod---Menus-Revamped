#include "../src/sorting/rules/PickerNavigation.h"
#include "../src/sorting/rules/ControllerPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>

int main()
{
	using namespace itemtags;
	int checks = 0;
	const auto check = [&](bool ok, const char* label) {
		++checks;
		if (!ok) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); }
	};
	check(Info(Tab::Potions).count == 5 && Info(Tab::PotionEffects).count == 8 && Info(Tab::PoisonEffects).count == 4, "root and potion submenu counts");
	const char* roots[]{ "All", "Potions", "Poisons", "Ingredients", "Tools" };
	const char* families[]{ "All potions", "Restore", "Fortify", "Ward", "Stealth", "Utility", "Cures", "Other" };
	for (unsigned i = 0; i < 5; ++i) check(Info(Tab::Potions).names[i] == std::string_view(roots[i]), "agreed Alchemy root");
	for (unsigned i = 0; i < 8; ++i) check(Info(Tab::PotionEffects).names[i] == std::string_view(families[i]), "agreed potion families");
	check(AlchemyTags(19) == AlchemyBit(Alchemy::Tools), "all apparatus in one Alchemy Tools group");
	check(AlchemyTags(25) == AlchemyBit(Alchemy::Ingredients), "all ingredients");
	check(AlchemyTags(40) == AlchemyBit(Alchemy::Potions), "potion root membership");
	for (auto type : { 19u, 25u, 40u }) {
		const auto tags = AlchemyTags(type) | (3u << 28);
		check(IncludesAll(3, 0, tags), "All Items Alchemy keeps every alchemy kind");
		check(!IncludesAll(4, 0, tags), "alchemy kinds do not enter Misc");
	}
	check(!Includes(Index(Alchemy::Potions), AlchemyTags(19)) && !Includes(Index(Alchemy::Potions), AlchemyTags(25)), "All potions excludes apparatus/ingredients");
	check(!Includes(Index(Alchemy::Restore), AlchemyTags(25)), "ingredient effects cannot masquerade as potions");
	// Independently extracted ALCH 000092F6 PotionFortifySpeedW uses FOAT.
	const std::uint32_t fortifySpeed[]{ Code('F', 'O', 'A', 'T') };
	check(PotionTags(fortifySpeed, 1, 0) == (AlchemyBit(Alchemy::Potions) | AlchemyBit(Alchemy::Fortify)), "actual Fortify Speed potion");
	const std::uint32_t mixed[]{ Code('F','O','A','T'), Code('R','E','S','P') };
	check(PotionTags(mixed, 2, 1) == (AlchemyBit(Alchemy::Potions) | AlchemyBit(Alchemy::Fortify) | AlchemyBit(Alchemy::Restore)),
		"mixed groups remain independent; one hostile effect is not a poison");
	check(PotionTags(nullptr, 0, 0) == (AlchemyBit(Alchemy::Potions) | AlchemyBit(Alchemy::Other)), "unknown/empty-effect potion is reachable through Other");
	// Parents/back/cancel preserve the applied choice. Tools and Ingredients are leaves.
	auto next = pickernav::Choose(Tab::Potions, Tab::Potions, 0, Index(Alchemy::Tools), 1);
	check(next.view == Tab::PotionEffects && !next.close && next.choice == Index(Alchemy::Tools), "enter Potions without applying");
	next = pickernav::Back(Tab::Potions, next.view, next.category, next.choice);
	check(next.view == Tab::Potions && next.cursor == 1 && !next.close && next.choice == Index(Alchemy::Tools), "Back returns to Alchemy without applying");
	next = pickernav::Back(Tab::Potions, next.view, next.category, next.choice);
	check(next.close && next.choice == Index(Alchemy::Tools), "Back at root closes only");
	for (unsigned leaf : { 0u, 3u, 4u }) {
		next = pickernav::Choose(Tab::Potions, Tab::Potions, 0, 5, leaf);
		check(next.close && next.choice == leaf, "All/Ingredients/Tools have no submenu");
	}
	for (unsigned i = 0; i < 8; ++i) {
		next = pickernav::Choose(Tab::Potions, Tab::PotionEffects, 0, 3, i);
		check(next.close && next.choice == PotionSelection(i) && next.category == 0, "direct potion leaf maps to flat tag");
		check(pickernav::Highlight(Tab::Potions, Tab::PotionEffects, 0, next.choice) == i, "potion highlight round-trip");
		check(std::wstring_view(SelectionIcon(Tab::Potions, next.choice)) == Info(Tab::PotionEffects).icons[i], "header follows actual potion leaf");
		check(pickernav::Highlight(Tab::Potions, Tab::Potions, 0, next.choice) == 1, "root highlights Potions for every family");
	}
	// All Items -> Alchemy -> Potions: three levels, not an invalid category 8.
	next = pickernav::Choose(Tab::AllItems, Tab::AllItems, 1, 9, 3);
	check(next.view == Tab::Potions && next.category == 1 && next.choice == 9 && !next.close, "enter Alchemy without changing weapon filter");
	next = pickernav::Choose(Tab::AllItems, next.view, next.category, next.choice, 1);
	check(next.view == Tab::PotionEffects && next.category == 1 && next.choice == 9 && !next.close, "enter nested Potions without changing filter");
	next = pickernav::Back(Tab::AllItems, next.view, next.category, next.choice);
	check(next.view == Tab::Potions && next.cursor == 1 && next.category == 1 && next.choice == 9 && !next.close, "nested Back preserves old filter");
	next = pickernav::Back(Tab::AllItems, next.view, next.category, next.choice);
	check(next.view == Tab::AllItems && next.cursor == 3 && !next.close, "second Back returns to All Items categories");
	for (unsigned i = 0; i < 8; ++i) {
		next = pickernav::Choose(Tab::AllItems, Tab::PotionEffects, 1, 9, i);
		check(next.category == 3 && next.choice == PotionSelection(i) && next.close, "All Items potion leaf applies category3");
		check(pickernav::Highlight(Tab::AllItems, Tab::PotionEffects, next.category, next.choice) == i, "All Items leaf highlight");
		check(pickernav::Highlight(Tab::AllItems, Tab::AllItems, next.category, next.choice) == 3, "All Items top highlight");
	}
	for (unsigned i : { 0u, 3u, 4u }) {
		next = pickernav::Choose(Tab::AllItems, Tab::Potions, 1, 9, i);
		check(next.category == 3 && next.choice == i && next.close, "All Items Alchemy leaf");
	}
	check(pickernav::Highlight(Tab::AllItems, Tab::PotionEffects, 1, 9) == 99, "unrelated selected category has no potion highlight");
	// Existing category/magic navigation and command ranges remain unchanged.
	for (auto root : { Tab::Weapons, Tab::Gear, Tab::Misc, Tab::MagicSchool, Tab::MagicEffect }) {
		const auto count = Info(root).count;
		for (unsigned i = 0; i < count; ++i) {
			next = pickernav::Choose(root, root, 0, 0, i);
			check(next.close && next.choice == i && next.view == root, "existing picker selection");
			check(pickergrid::IsSelection(static_cast<int>(i), count), "existing mouse/pad command valid");
		}
	}
	for (unsigned category : { 1u, 2u, 4u }) {
		const auto view = static_cast<Tab>(category - 1);
		for (unsigned i = 0; i < Info(view).count; ++i) {
			next = pickernav::Choose(Tab::AllItems, view, 3, 7, i);
			check(next.close && next.choice == i && next.category == category, "existing All Items leaf");
		}
	}
	next = pickernav::Choose(Tab::Potions, Tab::PotionEffects, 0, 3, 999);
	check(!next.close && next.choice == 3 && next.view == Tab::PotionEffects, "invalid input cannot select a filter");
	check(!Includes(999, 0xFFFFFFFFu), "invalid flat choice is bounded");
	for (auto view : { Tab::Potions, Tab::PotionEffects, Tab::PoisonEffects }) {
		const auto count = Info(view).count;
		for (unsigned i = 0; i < count; ++i) {
			check(pickerpad::Move(i, count, pickerpad::Direction::Right) < count, "controller right stays in view");
			check(pickerpad::Move(i, count, pickerpad::Direction::Down) < count, "controller down stays in view");
			check(Info(view).icons[i] != nullptr, "every new cell has an editable icon");
		}
	}
	check(MiscTags(27, 0xC, 0) == (1u << 3), "actual base repair hammer is Misc Tools");
	check(MiscTags(27, 0x0100000C, 0) == (1u << 4), "unrelated plugin-local ID0C is not a repair hammer");
	check(MiscTags(26, 0x2CF9F, 0) == (1u << 3), "actual torch is Misc Tools");
	check(MiscTags(42, 0x41FD3, 0) == (1u << 4), "sigil stone stays Other");
	std::printf("%d alchemy/navigation checks passed\n", checks);
}
