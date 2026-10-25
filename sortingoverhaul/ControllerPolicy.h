#pragma once
#include <cstdint>
#include <cstdlib>

// Pure controller rules. No engine calls, hooks, or game state.
namespace sortpad
{
	constexpr std::uint16_t Up = 0x0001, Down = 0x0002, Left = 0x0004, Right = 0x0008;
	constexpr std::uint16_t A = 0x1000, B = 0x2000, Y = 0x8000;
	constexpr int Deadzone = 16000;
	enum class Direction { None, Left, Right, Up, Down };
	struct Input { std::uint16_t buttons = 0; short x = 0, y = 0; };

	inline Direction Axis(const Input& input)
	{
		const int horizontal = !!(input.buttons & Right) - !!(input.buttons & Left);
		const int vertical = !!(input.buttons & Up) - !!(input.buttons & Down);
		if (horizontal || vertical) {
			if (horizontal && vertical) return Direction::None; // ambiguous diagonal D-pad
			return horizontal ? (horizontal > 0 ? Direction::Right : Direction::Left) :
				(vertical > 0 ? Direction::Up : Direction::Down);
		}
		// Opposing D-pad buttons cancel instead of falling through to the stick.
		if (input.buttons & (Left | Right | Up | Down)) return Direction::None;
		const int x = input.x, y = input.y;
		if (std::abs(x) <= Deadzone && std::abs(y) <= Deadzone) return Direction::None;
		return std::abs(x) >= std::abs(y) ? (x > 0 ? Direction::Right : Direction::Left) :
			(y > 0 ? Direction::Up : Direction::Down);
	}

	inline unsigned Move(unsigned cursor, unsigned count, Direction direction)
	{
		if (!count || count > 9) return 0;
		if (cursor >= count) cursor = 0;
		const unsigned row = cursor / 3, column = cursor % 3;
		for (unsigned step = 1; step <= 3; ++step) {
			unsigned r = row, c = column;
			switch (direction) {
			case Direction::Left: c = (column + 3 - step) % 3; break;
			case Direction::Right: c = (column + step) % 3; break;
			case Direction::Up: r = (row + 3 - step) % 3; break;
			case Direction::Down: r = (row + step) % 3; break;
			default: return cursor;
			}
			const auto candidate = r * 3 + c;
			if (candidate < count) return candidate;
		}
		return cursor;
	}

	class State
	{
	public:
		std::uint16_t pressed = 0;
		Direction pulse = Direction::None;

		void Sample(Input input, std::uint64_t now)
		{
			pressed = input.buttons & ~previous;
			previous = input.buttons;
			blocked &= input.buttons; // owned buttons remain hidden until released
			if (std::abs(int(input.x)) <= Deadzone && std::abs(int(input.y)) <= Deadzone) blockStick = false;
			if (neutralRequired) {
				pressed = 0; pulse = Direction::None;
				blocked |= input.buttons;
				blockStick = true;
				if (!input.buttons && Axis(input) == Direction::None) neutralRequired = false;
				return;
			}
			const auto next = Axis(input);
			pulse = Direction::None;
			if (next != direction) {
				direction = next; repeatAt = now + 350;
				pulse = next;
			} else if (next != Direction::None && now >= repeatAt) {
				pulse = next; repeatAt = now + 140;
			}
		}

		void Consume(Input input, std::uint16_t buttons, bool stick)
		{
			blocked |= input.buttons & buttons;
			if (stick) blockStick = true; // suppress even sub-deadzone drift while modal
		}

		Input Filter(Input input) const
		{
			input.buttons &= ~blocked;
			if (blockStick) input.x = input.y = 0;
			return input;
		}

		void Disconnect() { *this = {}; neutralRequired = true; }

	private:
		std::uint16_t previous = 0, blocked = 0;
		bool blockStick = false, neutralRequired = false;
		Direction direction = Direction::None;
		std::uint64_t repeatAt = 0;
	};
}
