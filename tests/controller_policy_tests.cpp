#include "../sortingoverhaul/ControllerPolicy.h"
#include <initializer_list>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
using namespace sortpad;
unsigned checks = 0;
void Check(bool ok) { ++checks; if (!ok) { std::printf("FAIL check %u\n", checks); std::exit(1); } }
int main()
{
	Check(Axis({}) == Direction::None);
	Check(Axis({0, 16000, -16000}) == Direction::None);
	Check(Axis({0, -32768, 0}) == Direction::Left);
	Check(Axis({0, 0, 32767}) == Direction::Up);
	Check(Axis({Left | Right, 32767, 0}) == Direction::None);
	Check(Axis({Left | Up}) == Direction::None);
	Check(Axis({Down, 32767, 0}) == Direction::Down);
	Check(Move(2, 5, Direction::Down) == 2);
	Check(Move(4, 5, Direction::Right) == 3);
	Check(Move(0, 8, Direction::Up) == 6);
	Check(Move(99, 0, Direction::Right) == 0);
	for (unsigned n = 1; n <= pickergrid::kMaxChoices; ++n)
		for (unsigned i = 0; i < n; ++i)
			for (auto d : {Direction::Left, Direction::Right, Direction::Up, Direction::Down})
				Check(Move(i, n, d) < n);
	State state;
	Input a{A};
	state.Sample(a, 0); Check(state.pressed == A);
	state.Consume(a, A, false); Check(state.Filter(a).buttons == 0);
	state.Sample(a, 1); Check(!state.pressed);
	Check(state.Filter(a).buttons == 0); // modal closed; held A must not equip
	state.Sample({}, 2); state.Sample(a, 3); Check(state.pressed == A);
	Check(state.Filter(a).buttons == A);
	state.Sample({Left}, 10); Check(state.pulse == Direction::Left);
	state.Sample({Left}, 359); Check(state.pulse == Direction::None);
	state.Sample({Left}, 360); Check(state.pulse == Direction::Left);
	state.Sample({Left}, 361); Check(state.pulse == Direction::None);
	state.Sample({Right}, 362); Check(state.pulse == Direction::Right);
	Input stick{0, -24000, 0};
	state.Sample(stick, 400); state.Consume(stick, 0, true);
	Check(state.Filter(stick).x == 0);
	state.Sample(stick, 401); Check(state.Filter(stick).x == 0);
	state.Sample({}, 402); Check(state.Filter(stick).x == -24000);
	state.Sample({B}, 500); state.Consume({B}, B, false);
	state.Disconnect(); state.Sample({B}, 501);
	Check(state.pressed == 0 && state.Filter({B}).buttons == 0);
	state.Sample({}, 502); state.Sample({B}, 503);
	Check(state.pressed == B && state.Filter({B}).buttons == B);
	state.Consume({0, 1000, 0}, 0, true);
	Check(state.Filter({0, 1000, 0}).x == 0); // no drift through a modal popup
	state.Sample({}, 504);
	Check(state.Filter({0, 1000, 0}).x == 1000);
	for (auto button : {A, B, Y, Left, Right, Up, Down}) {
		State latch;
		latch.Sample({button}, 0);
		latch.Consume({button}, button, false);
		for (unsigned t = 1; t < 5; ++t) {
			latch.Sample({button}, t);
			Check(latch.Filter({button}).buttons == 0);
		}
		latch.Sample({}, 5); latch.Sample({button}, 6);
		Check(latch.Filter({button}).buttons == button);
	}
	State pass;
	pass.Sample({Y | B, 2345, -1200}, 0);
	Check(pass.Filter({Y | B, 2345, -1200}).buttons == (Y | B));
	Check(pass.Filter({Y | B, 2345, -1200}).x == 2345);
	Check(pass.Filter({Y | B, 2345, -1200}).y == -1200);
	// Holding A before entering custom focus does not generate a new confirm.
	state.Sample({A}, 600); state.Sample({A}, 700); Check(!state.pressed);
	Check(Move(2, 18, Direction::Up) == 14);
	Check(Move(17, 18, Direction::Down) == 1);
	Check(Move(17, 18, Direction::Right) == 16);
	Check(Move(15, 18, Direction::Left) == 14);
	Check(Move(16, 17, Direction::Right) == 16);
	Check(Move(2, 17, Direction::Up) == 14); // skip nonexistent last-column cell
	Check(Move(99, 19, Direction::Right) == 0);
	for (unsigned count = 1; count <= pickergrid::kMaxChoices; ++count) {
		bool seen[pickergrid::kMaxChoices]{};
		seen[0] = true;
		for (unsigned round = 0; round < count; ++round)
			for (unsigned at = 0; at < count; ++at)
				if (seen[at]) for (auto d : {Direction::Left, Direction::Right, Direction::Up, Direction::Down})
					seen[Move(at, count, d)] = true;
		for (unsigned at = 0; at < count; ++at) Check(seen[at]);
	}
	// Independent reference: cycle only the real cells sharing this row/column.
	const auto referenceMove = [](unsigned cursor, unsigned count, Direction direction) {
		std::vector<unsigned> line;
		const unsigned columns = count <= 1 ? 1 : count <= 4 ? 2 : count <= 9 ? 3 : 4;
		const bool horizontal = direction == Direction::Left || direction == Direction::Right;
		for (unsigned i = 0; i < count; ++i)
			if (horizontal ? i / columns == cursor / columns : i % columns == cursor % columns) line.push_back(i);
		const auto at = static_cast<unsigned>(std::find(line.begin(), line.end(), cursor) - line.begin());
		const auto size = static_cast<unsigned>(line.size());
		const bool forward = direction == Direction::Right || direction == Direction::Down;
		return line[(at + (forward ? 1 : size - 1)) % size];
	};
	for (unsigned n = 1; n <= pickergrid::kMaxChoices; ++n)
		for (unsigned at = 0; at < n; ++at)
			for (auto d : {Direction::Left, Direction::Right, Direction::Up, Direction::Down})
				Check(Move(at, n, d) == referenceMove(at, n, d));
	for (bool enabled : { false, true })
		for (bool controller : { false, true })
			for (bool finalSelection : { false, true })
				Check(ShouldReturnToList(enabled, controller, finalSelection) == (enabled && controller && finalSelection));
	ReturnToList returning;
	Check(!returning.Pending());
	returning.Request();
	Check(returning.Update(A, true, 0) == ReturnAction::None && returning.Pending());
	Check(returning.Update(0, true, 10) == ReturnAction::PressY);
	Check(returning.Update(0, true, 20) == ReturnAction::PressY);
	Check(returning.Update(0, false, 30) == ReturnAction::None && !returning.Pending());
	Check(returning.Update(0, true, 40) == ReturnAction::None); // no second exit press
	returning.Request();
	Check(returning.Update(Y, true, 50) == ReturnAction::None && !returning.Pending());
	returning.Request();
	Check(returning.Update(B, true, 60) == ReturnAction::None && !returning.Pending());
	returning.Request();
	Check(returning.Update(0, true, 100) == ReturnAction::PressY);
	Check(returning.Update(0, true, 600) == ReturnAction::TimedOut && !returning.Pending());
	Check(returning.Update(0, true, 601) == ReturnAction::None); // warns once only
	returning.Request(); returning.Cancel();
	Check(returning.Update(0, true, 700) == ReturnAction::None);
	PacketStamp packets;
	Check(packets.Update(10, {}) == 10);
	Check(packets.Update(10, {}) == 10);
	Check(packets.Update(10, {Y}) == 11); // synthetic Y changes even with unchanged physical packet
	Check(packets.Update(10, {Y}) == 11); // repeated polls are one held press
	Check(packets.Update(10, {}) == 12); // release must be observable too
	Check(packets.Update(11, {}) == 13);
	Check(packets.Update(11, {0, 123, -42}) == 14);
	packets.Reset(); Check(packets.Update(0xFFFFFFFFu, {}) == 0xFFFFFFFFu);
	Check(packets.Update(0xFFFFFFFFu, {Y}) == 0); // defined uint32 wrap
	std::printf("%u controller policy checks passed\n", checks);
}
