#include "../src/sorting/rules/InventoryHeaderLayout.h"

#include <cstdio>

int main()
{
	using itemsort::Key;
	using namespace inventoryheaders;
	const Box list{100, 200, 725, 425};
	std::array<Box, kCount> columns{};
	columns[int(Key::Cost)] = {625, 244, 60, 30};
	columns[int(Key::Value)] = {685, 244, 90, 30};
	std::array<bool, kCount> offered{};
	for (auto key : itemsort::SpellPurchaseKeys()) offered[int(key)] = true;

	const auto plan = Arrange(7, list, 165, columns, offered, 1, {}, 500);
	const auto center = [](Box box) { return box.x + box.w / 2; };
	const auto& school = plan.sorts[int(Key::School)];
	const auto& name = plan.sorts[int(Key::Name)];
	const bool checks[] = {
		plan.enabled,
		plan.filters[2].w > 0 && plan.filters[0].w == 0 && plan.filters[1].w == 0,
		name.x == 165 && name.w > 0,
		!offered[int(Key::School)] && school.w == 0,
		plan.filters[2].x + plan.filters[2].w <= name.x,
		center(plan.sorts[int(Key::Cost)]) == center(columns[int(Key::Cost)]),
		center(plan.sorts[int(Key::Value)]) == center(columns[int(Key::Value)]),
		plan.guides.back().w > 0,
	};
	int failures = 0;
	for (bool passed : checks) failures += !passed;
	std::printf("%zu checks, %d failures\n", std::size(checks), failures);
	return failures != 0;
}
