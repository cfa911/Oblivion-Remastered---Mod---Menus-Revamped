// In-game Pack Builder Phase 1: pure rules of the overlay (KB mods/menus-revamped/evidence/in-game-pack-builder-plan.md,
// technique techniques/in-game-imgui-overlay.md): when the window closes by itself, what a focus loss releases, and where
// the gear button that opens it sits on screen (owner decision 2026-11-21: a mouse-only gear at the top-right corner of
// the player menu's item panel, approved from preview v2 at (1202, 189) on a 2560x1440 screen, 40 px).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "../src/builder/InputGate.h"
#if __has_include("../src/builder/OverlayRules.h")
#include "../src/builder/OverlayRules.h"
#endif
#if !__has_include("../src/builder/OverlayRules.h") || !defined(MR_OVERLAY_RULES_GEAR)
// Before the feature: nothing closes by itself and there is no gear.
namespace ingame {
#if !__has_include("../src/builder/OverlayRules.h")
struct Context { int menuMode = 0; int menu = 0; bool world = true; };
struct CloseWatch { void Open(const Context&) {} void Close() {} bool ShouldClose(const Context&) const { return false; } };
#endif
struct Rect { double x = 0, y = 0, w = 0, h = 0; bool Contains(double, double) const { return false; } };
inline Rect GearRect(double, double) { return {}; }
}
#endif
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", why); } }
bool Near(double a, double b) { return std::fabs(a - b) < 0.6; }
int main()
{
	using namespace ingame;
	{
		CloseWatch w;
		Context gameplay{ 1, 0, true }, inventory{ 3, 1, true }, loading{ 1, 0, false };
		Check(!w.ShouldClose(inventory), "closed window: never asked to close");
		w.Open(gameplay);
		Check(!w.ShouldClose(gameplay), "same game state: stays open");
		Check(w.ShouldClose(inventory), "a game menu opened under it: closes");
		w.Open(inventory);
		Check(!w.ShouldClose(inventory), "opened over a menu: stays open there");
		Check(w.ShouldClose(Context{ 3, 2, true }), "the game switched to another menu: closes");
		w.Open(gameplay);
		Check(w.ShouldClose(loading), "the world went away (loading screen, main menu): closes");
		w.Close();
		Check(!w.ShouldClose(loading), "after closing: no further close");
	}
	{
		KeyGate g;
		g.Consume(Input::KeyDown, 'W');
		g.SetOpen(true);
		g.LoseFocus();
		Check(g.Consume(Input::KeyUp, 'W'), "after a focus loss (alt-tab) the game owes no release: Windows/UE flush held keys themselves");
		g.SetOpen(false);
		g.Consume(Input::KeyDown, 'S');
		g.LoseFocus();
		g.SetOpen(true);
		Check(g.Consume(Input::KeyUp, 'S'), "a key held before alt-tab is not owed once the window opens afterwards");
	}
	{
		// a_viewportWidth in pixels, a_scale = pixels per viewport unit (UE's DPI scale: 1440 / 1080 on the owner's screen)
		const Rect owner = GearRect(2560, 1440.0 / 1080.0);
		Check(Near(owner.x + owner.w / 2, 1202) && Near(owner.y + owner.h / 2, 189), "owner's 2560x1440: centred where preview v2 put it");
		Check(Near(owner.w, 40) && Near(owner.h, 40), "owner's 2560x1440: 40 px square");
		const Rect hd = GearRect(1920, 1.0);
		Check(Near(hd.x + hd.w / 2, 901.5) && Near(hd.w, 30), "1920x1080: same place in viewport units, 30 units");
		const Rect wide = GearRect(3440, 1440.0 / 1080.0);
		Check(Near(wide.x + wide.w / 2, 1202 + (3440 - 2560) / 2.0) && Near(wide.y, owner.y), "ultrawide: stays beside the centred menu panel");
		Check(owner.Contains(1202, 189) && owner.Contains(owner.x, owner.y), "a click on the gear hits it");
		Check(!owner.Contains(1202, 240) && !owner.Contains(owner.x + owner.w + 1, 189), "a click beside it does not");
	}
	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
