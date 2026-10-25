#include "../sortingoverhaul/ControllerPolicy.h"
#include <initializer_list>
#include <cstdio>
#include <cstdlib>
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
	for (unsigned n = 1; n <= 9; ++n)
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
	std::printf("%u controller policy checks passed\n", checks);
}
