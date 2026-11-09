// Magic revamp (owner 2026-11-09): the Magic menu gets the Inventory's single header row. Geometry from the Magic
// probe (game 1.512.105.0, 1920x1080): list 146.4/306.4 757.9x591.7, native Cost button 776.2 (85 wide).
// Spell pages (picker kind 1): Effect left of the names (like the Inventory category), A-Z over the names, Type and
// School in cells left of Cost, Cost over its column: the same left-to-right order as the controller carousel.
// Active Effects (kind 6): A-Z over the names, Effect value / Category / School cells, Magnitude over its column.
#include <array>
#include <cmath>
#include <cstdio>
#include "../src/sorting/rules/InventoryHeaderLayout.h"

using namespace inventoryheaders;
using itemsort::Key;
static unsigned checks = 0, failures = 0;
static void Check(bool ok, const char* what)
{
	++checks;
	if (!ok) { ++failures; std::printf("FAIL: %s\n", what); }
}
static double Center(const Box& b) { return b.x + b.w / 2; }
static bool Near(double a, double b) { return std::abs(a - b) < 0.01; }

int main()
{
	const Box list{ 146.4, 306.4, 757.9, 591.7 };
	const double nameX = 206.4;
	std::array<Box, kCount> columns{};
	columns[int(Key::Cost)] = { 776.2, 310, 85, 40 };
	columns[int(Key::Magnitude)] = { 776.2, 310, 85, 40 };
	std::array<bool, kCount> spell{}, active{};
	spell[int(Key::Name)] = spell[int(Key::Cost)] = true;
	for (auto k : { Key::Name, Key::EffectValue, Key::Category, Key::School, Key::Magnitude }) active[int(k)] = true;

	{
		const auto plan = Arrange(1, list, nameX, columns, spell, 3, {}, 700);
		Check(plan.enabled, "spell pages get the single header row");
		Check(Near(plan.inset, kInset), "the spell list makes room for the header strip like the Inventory");
		const auto& name = plan.sorts[int(Key::Name)];
		const auto& cost = plan.sorts[int(Key::Cost)];
		Check(Near(name.x, nameX) && Near(name.y, list.y) && Near(name.h, kHeight), "A-Z sits over the spell names, on the strip");
		Check(Near(Center(cost), 776.2 + 42.5), "Cost is centred over the cost column");
		const auto& effect = plan.filters[0];
		const auto& type = plan.filters[1];
		const auto& school = plan.filters[2];
		Check(effect.w > 0 && effect.x >= list.x && effect.x + effect.w <= nameX - 3, "Effect sits left of the names, like the Inventory category");
		Check(type.w > 0 && school.w > 0, "Type and School are placed");
		Check(Near(Center(school), 776.2 - 27) && Near(Center(type), 776.2 - 81), "Type and School fill the two 54-unit cells left of Cost");
		Check(Center(effect) < Center(name) && Center(name) < Center(type) && Center(type) < Center(school) && Center(school) < Center(cost),
			"left to right: Effect, A-Z, Type, School, Cost (the controller carousel order)");
		for (const auto* b : { &effect, &type, &school }) Check(Near(b->y, list.y) && Near(b->h, kHeight), "the pickers sit on the strip");
		const auto& top = plan.guides[kGuides - 2];
		const auto& bottom = plan.guides.back();
		Check(Near(top.x, list.x) && Near(top.y, list.y) && Near(top.x + top.w, 776.2 + 85), "upper rule runs from the list start to the cost column end");
		Check(Near(bottom.y, list.y + kHeight), "lower rule closes the strip");
		bool nameDivider = false, costDivider = false, schoolDivider = false, typeDivider = false;
		for (int i = 0; i < kListGuide; ++i) {
			if (!plan.rule[i]) continue;
			nameDivider |= Near(plan.guides[i].x, nameX - 3);
			costDivider |= Near(plan.guides[i].x, 776.2);
			schoolDivider |= Near(plan.guides[i].x, 776.2 - 54);
			typeDivider |= Near(plan.guides[i].x, 776.2 - 108);
		}
		Check(nameDivider && costDivider && schoolDivider && typeDivider, "dividers before the names, each picker cell and Cost");
		Check(Near(plan.guides[kListGuide].x, nameX - 3) && Near(plan.guides[kListGuide + 1].x, 776.2), "list guides continue the name and cost dividers");
		const auto fill = ControlFill(plan, type);
		Check(Near(fill.w, 54 - kRule) || Near(fill.w, 54), "the Type highlight fills its cell");
		const Box glyph = HintBox(plan, { 508.2, 259.9, 40, 40 });
		Check(glyph.x >= top.x + top.w && glyph.x <= top.x + top.w + 16, "the controller Y glyph goes to the row end");
	}
	{
		std::array<Box, kCount> none{};
		const auto plan = Arrange(1, list, nameX, none, spell, 3, {}, 0);
		Check(plan.enabled && Near(plan.sorts[int(Key::Cost)].x + kButton / 2 + 87 / 2.0, list.x + list.w - 22),
			"an unmeasured cost column is estimated from the list's right edge");
	}
	{
		const auto plan = Arrange(6, list, nameX, columns, active, 0, {}, 700);
		Check(plan.enabled, "Active Effects gets the single header row");
		const auto& name = plan.sorts[int(Key::Name)];
		const auto& value = plan.sorts[int(Key::EffectValue)];
		const auto& category = plan.sorts[int(Key::Category)];
		const auto& school = plan.sorts[int(Key::School)];
		const auto& magnitude = plan.sorts[int(Key::Magnitude)];
		Check(Near(name.x, nameX), "A-Z sits over the effect names");
		Check(Near(Center(magnitude), 776.2 + 42.5), "Magnitude is centred over its column");
		Check(Near(Center(school), 776.2 - 27) && Near(Center(category), 776.2 - 81) && Near(Center(value), 776.2 - 135),
			"Effect value, Category and School fill the cells left of Magnitude");
		Check(Center(name) < Center(value) && Center(value) < Center(category) && Center(category) < Center(school) &&
			Center(school) < Center(magnitude), "left to right: A-Z, Effect value, Category, School, Magnitude (carousel order)");
		Check(plan.filters[0].w == 0 && plan.filters[1].w == 0 && plan.filters[2].w == 0, "Active Effects has no pickers");
	}
	{
		// RMI-080 (owner screenshots 2026-11-09): the rows have their own spell-type and school columns; the Type and School
		// pickers (and the Active Effects sorts) sit over those measured columns, with the dividers on the column edges.
		auto measured = columns;
		measured[int(Key::Category)] = { 690.0, 310, 40, 40 };   // spell type (star) column
		measured[int(Key::School)] = { 735.0, 310, 38, 40 };     // school column
		const auto plan = Arrange(1, list, nameX, measured, spell, 3, {}, 700);
		Check(Near(Center(plan.filters[1]), 710.0) && Near(Center(plan.filters[2]), 754.0), "Type and School sit over their measured row columns");
		bool typeEdge = false, schoolEdge = false;
		for (int i = 0; i < kListGuide; ++i) {
			if (!plan.rule[i]) continue;
			typeEdge |= Near(plan.guides[i].x, 690.0);
			schoolEdge |= Near(plan.guides[i].x, 735.0);
		}
		Check(typeEdge && schoolEdge, "the dividers follow the measured column edges");
		measured[int(Key::EffectValue)] = { 640.0, 310, 44, 40 };
		const auto act = Arrange(6, list, nameX, measured, active, 0, {}, 700);
		Check(Near(Center(act.sorts[int(Key::EffectValue)]), 662.0) && Near(Center(act.sorts[int(Key::Category)]), 710.0) &&
			Near(Center(act.sorts[int(Key::School)]), 754.0), "Active Effects sorts sit over their measured row columns");
	}
	{
		Check(!Arrange(2, list, nameX, columns, spell, 3).enabled && !Arrange(5, list, nameX, columns, spell, 3).enabled,
			"the Effect and Type pickers are laid out by the School host, not on their own");
		Check(!Arrange(3, list, nameX, columns, spell, 3).enabled, "container sides keep the native row");
	}
	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
