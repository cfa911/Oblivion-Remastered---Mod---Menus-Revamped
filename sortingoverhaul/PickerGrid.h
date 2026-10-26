#pragma once

// Shared, pure geometry/command limits for mouse layout and controller navigation.
namespace pickergrid
{
	constexpr unsigned kMaxChoices = 18;
	constexpr unsigned CellWidth = 104, CellHeight = 62, ColumnStride = CellWidth + 4;
	constexpr int kToggleCommand = -4; // negative: never collide with choice 9 or any future choice
	constexpr unsigned Columns(unsigned count) { return count <= 1 ? 1 : count <= 4 ? 2 : count <= 9 ? 3 : 4; }
	constexpr unsigned Rows(unsigned count) { return (count + Columns(count) - 1) / Columns(count); }
	constexpr unsigned Width(unsigned count) { return 16 + Columns(count) * CellWidth + (Columns(count) - 1) * 4; }
	constexpr unsigned Height(unsigned count) { return 44 + 66 * Rows(count); }
	constexpr unsigned CellX(unsigned index, unsigned count) { return 8 + (index % Columns(count)) * ColumnStride; }
	constexpr unsigned CellY(unsigned index, unsigned count) { return 34 + (index / Columns(count)) * 66; }
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
	constexpr double PopupLeft(double headerX, double headerWidth, double canvasWidth, unsigned count)
	{
		// Keep the opened header one grid column into the popup, not at its right edge.
		return Position(headerX + headerWidth / 2.0 - ColumnStride, Width(count), canvasWidth);
	}
}
