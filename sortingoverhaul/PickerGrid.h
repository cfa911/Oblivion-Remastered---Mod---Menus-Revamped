#pragma once

// Shared, pure geometry/command limits for mouse layout and controller navigation.
namespace pickergrid
{
	constexpr unsigned kColumns = 3, kMaxChoices = 18;
	constexpr int kToggleCommand = -4; // negative: never collide with choice 9 or any future choice
	constexpr unsigned Rows(unsigned count) { return (count + kColumns - 1) / kColumns; }
	constexpr unsigned Width = 244;
	constexpr unsigned Height(unsigned count) { return 44 + 66 * (Rows(count) < 3 ? 3 : Rows(count)); }
	constexpr unsigned CellX(unsigned index) { return 8 + (index % kColumns) * 76; }
	constexpr unsigned CellY(unsigned index) { return 34 + (index / kColumns) * 66; }
	constexpr bool IsSelection(int command, unsigned count)
	{
		return command >= 0 && count <= kMaxChoices && static_cast<unsigned>(command) < count;
	}
	constexpr double Position(double preferred, double extent, double available)
	{
		if (available <= 0) return preferred < 0 ? 0.0 : preferred;
		const double maximum = available > extent ? available - extent : 0.0;
		return preferred < 0 ? 0.0 : (preferred > maximum ? maximum : preferred);
	}
}
