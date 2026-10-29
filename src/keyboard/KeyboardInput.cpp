#include "keyboard/KeyboardInput.h"

#include "keyboard/KeyboardConfig.h"
#include "engine/Menus.h"
#include "engine/Reflect.h"
#include "keyboard/SearchApi.h"
#include "keyboard/Keyboard.h"

#include <Xinput.h>   // types and constants only - nothing is linked or loaded

namespace keyboardinput
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr int kPulseReads = 4;

		// ---- state, touched only inside the read (the game thread) ----
		WORD        g_prevRaw = 0;
		BYTE        g_prevOutLT = 0, g_prevOutRT = 0;
		SHORT       g_prevOutRX = 0, g_prevOutRY = 0;
		menus::Menu g_prevMenu = menus::Menu::kNone;

		bool              g_held = false;    // the keyboard's button went down in a menu with a search box
		bool              g_fired = false;   // ... and the keyboard opened for it
		Clock::time_point g_at{};
		int               g_pulse = 0;       // reads left of a replayed tap
		WORD              g_btn = 0;         // the button being timed (D-pad right; in the container / barter menu also D-pad left)
		menus::Menu       g_side = menus::Menu::kNone;   // the menu (or the container menu's side) it opens the keyboard for
		WORD              g_pulseBtn = 0;    // the button of the replayed tap

		// the engine lookups run here, on the game thread, about every 200 ms (never from a thread of our own: a start-up
		// crashed in UObjectArray that way, 2026-09-29)


		void Rewrite(XINPUT_GAMEPAD& a_pad, bool allowOpen)
		{
			const WORD raw = a_pad.wButtons;
			const WORD pressed = raw & static_cast<WORD>(~g_prevRaw);
			g_prevRaw = raw;
			const auto now = Clock::now();
			const auto menu = search::Context();   // the Inventory, the Magic menu, or the container / barter menu (as kContainerYours)
			const auto& c = kbcfg::Get();

			if (menu != g_prevMenu) {   // a menu opened or closed (or the tab changed): the keyboard goes with it, a half-timed hold is forgotten
				g_held = false;
				g_fired = false;
				keyboard::Close("menu changed");
				g_prevMenu = menu;
			}


			// the keyboard has the pad: the menu stands still, the game sees an idle controller
			if (keyboard::Taking()) {
				keyboard::Rewrite(raw, a_pad.sThumbLX, a_pad.sThumbLY, now);
				a_pad.wButtons = 0;
				// the triggers go through: LT / RT switch the menu's tabs and the search follows the list (the keyboard itself does not use them)
				a_pad.sThumbLX = a_pad.sThumbLY = a_pad.sThumbRX = a_pad.sThumbRY = 0;
				return;
			}

			WORD out = raw;
			out &= static_cast<WORD>(~keyboard::Swallow(raw));   // held when the keyboard closed (B, Start...): the game never sees them

			// A hold on the button opens the keyboard; a tap is replayed after the release, so the game's own use of the button
			// (stepping the sort) is kept.
				if (allowOpen && c.enabled && menu != menus::Menu::kNone) {
					const bool  container = menu == menus::Menu::kContainerYours;
					const WORD  right = static_cast<WORD>(c.button), left = static_cast<WORD>(container ? c.buttonLeft : 0);
					if ((right && (pressed & right)) || (left && (pressed & left))) {
						g_btn = left && (pressed & left) ? left : right;
						g_side = menu;
						if (container) {   // right = your side, left = the other; a plain container has only its own list, which either button opens
							const bool yoursOk = keyboard::CanOpen(menus::Menu::kContainerYours), theirsOk = keyboard::CanOpen(menus::Menu::kContainerTheirs);
							g_side = yoursOk && theirsOk ? (g_btn == left ? menus::Menu::kContainerTheirs : menus::Menu::kContainerYours) : yoursOk ? menus::Menu::kContainerYours : menus::Menu::kContainerTheirs;
						}
						g_fired = false;
						g_at = now;
						g_held = keyboard::CanOpen(g_side);   // the keyboard cannot open here (no Menu Search plugin, or its search is off in this menu): the game keeps the button
					}
					if (g_held) {
						out &= static_cast<WORD>(~g_btn);
						if (!g_fired && (raw & g_btn) && now - g_at >= std::chrono::milliseconds(c.holdMs)) {
							g_fired = true;
							keyboard::Open(g_side, raw);
						}
						if (!(raw & g_btn)) {
							if (!g_fired) {   // a tap: the game gets it now
								g_pulse = kPulseReads;
								g_pulseBtn = g_btn;
							}
							g_held = false;
						}
					}
				}
				if (g_pulse > 0) {
					out |= g_pulseBtn;
					--g_pulse;
				}
			a_pad.wButtons = out;
		}

	}

	void Process(XINPUT_STATE& state, bool allowOpen, bool connected)
	{
		if (!connected) {
			keyboard::Close("controller disconnected");
			g_prevRaw = 0;
			g_held = g_fired = false;
			g_pulse = 0; g_prevMenu = menus::Menu::kNone;
			return;
		}
		if (!allowOpen && !keyboard::Taking()) { g_held = g_fired = false; g_pulse = 0; }
		Rewrite(state.Gamepad, allowOpen);
	}
}
