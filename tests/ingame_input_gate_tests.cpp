// In-game Pack Builder Phase 0: what the overlay's window procedure and XInput gate hide from the game while the builder
// window is open. KB technique page techniques/in-game-imgui-overlay.md (release rule, neutral pad until every button is up).
#include <cstdint>
#include <cstdio>
#if __has_include("../src/builder/InputGate.h")
#include "../src/builder/InputGate.h"
#else
// Before the feature: no gate, the game sees every input.
namespace ingame {
enum class Input { KeyDown, KeyUp, Char, MouseMove, MouseDown, MouseUp, Wheel, Raw, Other };
struct KeyGate {
	void SetOpen(bool) {}
	bool Consume(Input, unsigned) { return false; }
};
struct Pad { std::uint32_t packet = 0; std::uint16_t buttons = 0; std::uint8_t lt = 0, rt = 0; std::int16_t lx = 0, ly = 0, rx = 0, ry = 0; };
struct PadGate {
	void Filter(bool, Pad&) {}
};
}
#endif
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", why); } }
int main()
{
	using namespace ingame;
	{
		KeyGate g;
		Check(!g.Consume(Input::KeyDown, 'W'), "closed: the game gets key presses");
		g.SetOpen(true);
		Check(g.Consume(Input::KeyDown, 'E'), "open: a key press is hidden from the game");
		Check(g.Consume(Input::KeyUp, 'E'), "open: the release of a key pressed while open is hidden too");
		Check(!g.Consume(Input::KeyUp, 'W'), "open: the release of a key the game saw pressed reaches it (no stuck key)");
		Check(g.Consume(Input::KeyUp, 'W'), "that release passes once only");
		Check(g.Consume(Input::Char, 'a') && g.Consume(Input::MouseMove, 0) && g.Consume(Input::Wheel, 0) && g.Consume(Input::Raw, 0),
			"open: characters, mouse moves, wheel and raw input are hidden");
		Check(g.Consume(Input::MouseDown, 0) && g.Consume(Input::MouseUp, 0), "open: clicks inside the window are hidden");
		Check(!g.Consume(Input::Other, 0), "open: non-input messages always reach the game");
		g.SetOpen(false);
		Check(!g.Consume(Input::KeyDown, 'E') && !g.Consume(Input::KeyUp, 'E'), "closed again: keys reach the game");
	}
	{
		KeyGate g;
		g.Consume(Input::MouseDown, 1);   // right button held by the game (block) when the window opens
		g.SetOpen(true);
		Check(!g.Consume(Input::MouseUp, 1), "a mouse button held before opening is released to the game");
	}
	{
		PadGate p;
		Pad live{ 7, 0x1000, 0, 0, 0, 0, 0, 0 };   // A held
		Pad seen = live;
		p.Filter(false, seen);
		Check(seen.buttons == 0x1000 && seen.packet == 7, "closed: the game reads the real pad");
		seen = live;
		p.Filter(true, seen);
		Check(seen.buttons == 0 && seen.lx == 0 && seen.rt == 0, "open: the game reads an empty pad");
		Check(seen.packet != 7, "open: the packet number changes so the game registers the release");
		seen = live;
		p.Filter(false, seen);
		Check(seen.buttons == 0, "just closed with A still held: the pad stays empty (the closing press never fires)");
		Pad up{ 9, 0, 0, 0, 0, 0, 0, 0 };
		seen = up;
		p.Filter(false, seen);
		seen = live;
		p.Filter(false, seen);
		Check(seen.buttons == 0x1000, "after every button was up once, the real pad is back");
	}
	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
