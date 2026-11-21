#pragma once
// In-game Pack Builder Phase 1: pure rules of the overlay (unit-tested in tests/ingame_overlay_rules_tests.cpp).
// KB mods/menus-revamped/evidence/in-game-pack-builder-plan.md, technique techniques/in-game-imgui-overlay.md.
// - CloseWatch: the window closes by itself when the game state under it changes (a menu opens, closes or switches,
//   the world goes away for a loading screen or the main menu).
// - GearRect: where the gear button that opens the window sits (owner decision 2026-11-21: mouse only, top-right corner
//   of the player menu's item panel; preview v2 approved at (1202, 189), 40 px, on a 2560x1440 screen). The player menu
//   is centred, so the gear is placed from the screen's horizontal centre in viewport units.
#define MR_OVERLAY_RULES_GEAR 1

namespace ingame
{
	// What the window is open over: the Gamebryo menu mode (1 = gameplay), the active Menus Revamped menu id, and
	// whether a world exists (no world = loading screen or main menu).
	struct Context
	{
		int menuMode = 0;
		int menu = 0;
		bool world = true;
	};

	class CloseWatch
	{
	public:
		void Open(const Context& a_context)
		{
			open_ = true;
			at_ = a_context;
		}
		void Close() { open_ = false; }

		bool ShouldClose(const Context& a_now) const
		{
			if (!open_) return false;
			return !a_now.world || a_now.menuMode != at_.menuMode || a_now.menu != at_.menu;
		}

	private:
		bool open_ = false;
		Context at_{};
	};

	struct Rect
	{
		double x = 0, y = 0, w = 0, h = 0;
		bool Contains(double a_x, double a_y) const { return a_x >= x && a_x < x + w && a_y >= y && a_y < y + h; }
	};

	// Gear layout in viewport units (1080 units high): centre 58.5 units left of the screen's centre, 141.75 down, 30 square.
	inline constexpr double kGearOffsetX = -58.5;
	inline constexpr double kGearCentreY = 141.75;
	inline constexpr double kGearSize = 30.0;

	// The gear in pixels. a_viewportWidth in pixels, a_scale = pixels per viewport unit (WidgetLayoutLibrary.GetViewportScale).
	inline Rect GearRect(double a_viewportWidth, double a_scale)
	{
		const double size = kGearSize * a_scale;
		const double cx = a_viewportWidth / 2.0 + kGearOffsetX * a_scale;
		const double cy = kGearCentreY * a_scale;
		return { cx - size / 2.0, cy - size / 2.0, size, size };
	}
}
