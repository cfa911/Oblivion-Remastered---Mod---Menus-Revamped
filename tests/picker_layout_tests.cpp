#include "../sortingoverhaul/ItemTags.h"
#include <cstdio>
#include <cstdlib>

int main()
{
	unsigned checks = 0;
	const auto check = [&](bool ok, const char* name) {
		++checks;
		if (!ok) { std::fprintf(stderr, "FAIL %s\n", name); std::exit(1); }
	};
	check(pickergrid::Columns(4) == 2 && pickergrid::Width(4) == 228, "up to four filters use two columns");
	check(pickergrid::Columns(1) == 1 && pickergrid::Rows(1) == 1 && pickergrid::Width(1) == 120, "one filter is exactly one by one");
	check(pickergrid::Columns(5) == 3 && pickergrid::Columns(9) == 3 && pickergrid::Width(9) == 336, "five through nine filters use three columns");
	check(pickergrid::Columns(10) == 4 && pickergrid::Width(18) == 444, "larger pickers use four columns");
	check(pickergrid::PopupLeft(600, 48, 1280, 18) == 516, "green-arrow anchor shifts popup right of old right alignment");
	check(pickergrid::PopupLeft(0, 48, 1280, 18) == 0, "left edge remains on canvas");
	check(pickergrid::PopupLeft(1250, 48, 1280, 18) == 836, "right edge remains on canvas");
	check(pickergrid::PopupLeft(1250, 48, 1280, 4) == 1052, "clamp uses the current narrower popup width");
	check(pickergrid::PopupLeft(0, 48, 300, 18) == 0, "undersized canvas remains bounded");
	check(pickergrid::Height(4) == 176, "four-filter Alchemy uses two rows");
	check(pickergrid::Height(5) == 176 && pickergrid::Height(6) == 176 && pickergrid::Height(7) == 242, "six filters use three by two");
	check(pickergrid::Columns(9) == 3 && pickergrid::Rows(9) == 3, "nine filters use three by three");
	check(pickergrid::Height(13) == 308 && pickergrid::Height(18) == 374, "weapons/effects grow only as needed");
	for (unsigned count = 1; count <= pickergrid::kMaxChoices; ++count) {
		const auto columns = count <= 1 ? 1u : count <= 4 ? 2u : count <= 9 ? 3u : 4u;
		check(pickergrid::Columns(count) == columns, "owner column-count rule");
		check(pickergrid::Rows(count) == (count + columns - 1) / columns, "dynamic ceiling rows");
		for (unsigned i = 0; i < count; ++i) {
			check(pickergrid::CellX(i, count) + pickergrid::CellWidth <= pickergrid::Width(count) - 8, "cell inside right padding");
			check(pickergrid::CellY(i, count) + pickergrid::CellHeight < pickergrid::Height(count), "cell inside actual panel height");
		}
		check(pickergrid::Height(count) - (pickergrid::CellY(count - 1, count) + pickergrid::CellHeight) == 14, "no unnecessary spare rows");
	}
	using itemtags::Tab;
	for (auto tab : { Tab::Weapons, Tab::Gear, Tab::Potions, Tab::Misc, Tab::AllItems, Tab::MagicSchool, Tab::MagicEffect, Tab::PotionEffects }) {
		const auto& info = itemtags::Info(tab);
		for (unsigned i = 0; i < info.count; ++i) {
			const auto label = itemtags::PopupLabel(tab, i);
			check(!label.empty() && label.find("...") == std::string_view::npos, "no forced dotted fragments");
			if (tab != Tab::MagicSchool && tab != Tab::MagicEffect)
				check(label == info.names[i], "ordinary filters keep full names");
		}
		check(itemtags::PopupLabel(tab, info.count).empty(), "invalid label bounded");
	}
	check(itemtags::PopupLabel(Tab::Potions, 2) == "Ingredients", "Ingredients is readable");
	check(itemtags::PopupLabel(Tab::PotionEffects, 4) == "Attributes", "Attributes is readable");
	check(itemtags::PopupLabel(Tab::Misc, 2) == "Soul gems", "Soul gems is readable");
	check(itemtags::PopupLabel(Tab::PotionEffects, 0) == "All potions", "All potions is readable");
	check(itemtags::PopupLabel(Tab::MagicSchool, 1) == "Alte", "owner School abbreviations unchanged");
	std::printf("%u picker layout checks passed\n", checks);
}
