#pragma once
#include <cstdint>
#include <cstdlib>
#include "sorting/rules/PickerGrid.h"

// Pure controller rules. No engine calls, hooks, or game state.
namespace pickerpad
{
	constexpr std::uint16_t Up = 0x0001, Down = 0x0002, Left = 0x0004, Right = 0x0008;
	constexpr std::uint16_t A = 0x1000, B = 0x2000, Y = 0x8000;
	constexpr int Deadzone = 16000;
	enum class Direction { None, Left, Right, Up, Down };
	struct Input { std::uint16_t buttons = 0; short x = 0, y = 0, rx = 0, ry = 0; std::uint8_t lt = 0, rt = 0; };

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
		if (!count || count > pickergrid::kMaxChoices) return 0;
		if (cursor >= count) cursor = 0;
		const unsigned columns = pickergrid::Columns(count), rows = pickergrid::Rows(count);
		const unsigned row = cursor / columns, column = cursor % columns;
		const unsigned steps = direction == Direction::Left || direction == Direction::Right ? columns : rows;
		for (unsigned step = 1; step <= steps; ++step) {
			unsigned r = row, c = column;
			switch (direction) {
			case Direction::Left: c = (column + columns - step) % columns; break;
			case Direction::Right: c = (column + step) % columns; break;
			case Direction::Up: r = (row + rows - step) % rows; break;
			case Direction::Down: r = (row + step) % rows; break;
			default: return cursor;
			}
			const auto candidate = r * columns + c;
			if (candidate < count) return candidate;
		}
		return cursor;
	}

	constexpr bool ShouldReturnToList(bool enabled, bool controllerSelection, bool finalSelection)
	{
		return enabled && controllerSelection && finalSelection;
	}

	enum class ReturnAction { None, PressY, TimedOut };
	class ReturnToList
	{
	public:
		void Request() { pending = true; pressing = false; }
		void Cancel() { pending = pressing = false; }
		bool Pending() const { return pending; }
		ReturnAction Update(std::uint16_t buttons, bool sortingFocused, std::uint64_t now)
		{
			if (!pending) return ReturnAction::None;
			if (!sortingFocused || (buttons & (Y | B))) { Cancel(); return ReturnAction::None; }
			if (!pressing) {
				// Never expose the selecting A press to the item list.
				if (buttons & A) return ReturnAction::None;
				pressing = true; deadline = now + 500;
			}
			if (now >= deadline) { Cancel(); return ReturnAction::TimedOut; }
			// One held native Y press until focus acknowledges it, never repeated taps.
			return ReturnAction::PressY;
		}
	private:
		bool pending = false, pressing = false;
		std::uint64_t deadline = 0;
	};

	// XInput packet IDs must also change for our synthetic press AND its release.
	class PacketStamp
	{
	public:
		void Reset() { have = false; }
		std::uint32_t Update(std::uint32_t nativePacket, Input forwarded)
		{
			if (!have) { packet = nativePacket; have = true; }
			else if (nativePacket != lastNative || forwarded.buttons != last.buttons || forwarded.x != last.x || forwarded.y != last.y ||
				forwarded.rx != last.rx || forwarded.ry != last.ry || forwarded.lt != last.lt || forwarded.rt != last.rt)
				++packet;
			lastNative = nativePacket; last = forwarded;
			return packet;
		}
	private:
		bool have = false;
		std::uint32_t packet = 0, lastNative = 0;
		Input last{};
	};

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
