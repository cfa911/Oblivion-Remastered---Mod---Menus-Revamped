#pragma once
// In-game Pack Builder: what the game must not see while the builder window is open (pure logic, unit-tested in
// tests/ingame_input_gate_tests.cpp). KB techniques/in-game-imgui-overlay.md.
// - Keys and mouse: everything is hidden while open, except the release of a key or button the game saw pressed before
//   the window opened (otherwise it stays held in the game: a stuck key).
// - Pad: while open the game reads an empty pad with a changed packet number (so it registers the release); after closing
//   it keeps reading empty until every button and trigger was up once, so the press that closed the window never fires.
#include <bitset>
#include <cstdint>

namespace ingame
{
	enum class Input { KeyDown, KeyUp, Char, MouseMove, MouseDown, MouseUp, Wheel, Raw, Other };

	class KeyGate
	{
	public:
		void SetOpen(bool a_open)
		{
			if (a_open && !open_) {
				owedRelease_ = held_;   // the game saw these pressed: their releases still belong to it
			}
			open_ = a_open;
		}

		bool IsOpen() const { return open_; }

		// true = hide this message from the game. a_code: virtual key for keys, button 0-4 for the mouse.
		bool Consume(Input a_input, unsigned a_code)
		{
			const unsigned slot = Slot(a_input, a_code);
			if (!open_) {
				if (a_input == Input::KeyDown || a_input == Input::MouseDown) {
					held_.set(slot);
				} else if (a_input == Input::KeyUp || a_input == Input::MouseUp) {
					held_.reset(slot);
				}
				return false;
			}
			switch (a_input) {
			case Input::KeyUp:
			case Input::MouseUp:
				if (owedRelease_.test(slot)) {
					owedRelease_.reset(slot);
					held_.reset(slot);
					return false;
				}
				return true;
			case Input::Other:
				return false;
			default:
				return true;
			}
		}

	private:
		static unsigned Slot(Input a_input, unsigned a_code)
		{
			const bool mouse = a_input == Input::MouseDown || a_input == Input::MouseUp;
			return mouse ? 256 + (a_code & 7) : (a_code & 255);
		}

		std::bitset<264> held_;
		std::bitset<264> owedRelease_;
		bool open_ = false;
	};

	// The XINPUT_GAMEPAD fields the gate touches, plus the packet number (kept independent of <Xinput.h> for the tests).
	struct Pad
	{
		std::uint32_t packet = 0;
		std::uint16_t buttons = 0;
		std::uint8_t lt = 0, rt = 0;
		std::int16_t lx = 0, ly = 0, rx = 0, ry = 0;
	};

	class PadGate
	{
	public:
		void Filter(bool a_open, Pad& a_pad)
		{
			const bool idle = a_pad.buttons == 0 && a_pad.lt < kTriggerUp && a_pad.rt < kTriggerUp;
			if (a_open) {
				waitRelease_ = true;
			} else if (waitRelease_ && idle) {
				waitRelease_ = false;
			}
			if (a_open || waitRelease_) {
				const std::uint32_t packet = a_pad.packet + kPacketShift;
				a_pad = Pad{};
				a_pad.packet = packet;
			}
		}

	private:
		static constexpr std::uint8_t kTriggerUp = 30;          // XINPUT_GAMEPAD_TRIGGER_THRESHOLD
		static constexpr std::uint32_t kPacketShift = 0x40000000;
		bool waitRelease_ = false;
	};
}
