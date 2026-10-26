#include "../sortingoverhaul/ItemTags.h"
#include <iostream>
#include <string_view>

int main()
{
	const auto& all = itemtags::Info(itemtags::Tab::AllItems);
	if (std::string_view(all.names[3]) != "Alchemy" ||
		std::wstring_view(all.icons[3]) != L"Category-Alchemy.png" ||
		std::wstring_view(itemtags::SelectionIcon(itemtags::Tab::AllItems, 3)) != L"Category-Alchemy.png") {
		std::cerr << "FAIL: All Items / Alchemy must use Category-Alchemy.png\n";
		return 1;
	}
	std::cout << "PASS: Alchemy popup and selection icon use Category-Alchemy.png\n";
	if (std::wstring_view(itemtags::Info(itemtags::Tab::Potions).icons[0]) != L"Alchemy-All.png" ||
		std::wstring_view(itemtags::SelectionIcon(itemtags::Tab::Potions, 0)) != L"Alchemy-All.png") {
		std::cerr << "FAIL: Alchemy / All must use Alchemy-All.png\n";
		return 1;
	}
	if (std::wstring_view(itemtags::Info(itemtags::Tab::PotionEffects).icons[0]) != L"Potion-All.png" ||
		std::wstring_view(itemtags::SelectionIcon(itemtags::Tab::Potions, itemtags::Index(itemtags::Alchemy::Potions))) != L"Potion-All.png") {
		std::cerr << "FAIL: All potions must use Potion-All.png\n";
		return 1;
	}
	std::cout << "PASS: Alchemy-All.png and Potion-All.png mappings\n";
}
