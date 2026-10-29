#include "../src/sorting/rules/PickerHighlight.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

unsigned checks = 0;
void Check(bool ok) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL highlight check %u\n", checks); std::exit(1); } }
int main()
{
	using namespace pickerhighlight;
	const int expected[] = { 0xE6, 0xE4, 0xDF };
	for (unsigned i = 0; i < 3; ++i) {
		const double linear = kFill[i];
		const double srgb = linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
		Check(static_cast<int>(std::lround(srgb * 255)) == expected[i]);
		Check(linear > 0 && linear < 1);
	}
	Check(kFill[3] == 1.0f);
	Check(kOutlineWidth == 0);
	Check(kCornerRadius == 0);
	NativeHandoff state;
	Check(state.Update(false, false, false) == Transition::None);
	Check(state.Update(true, false, false) == Transition::Enter);
	for (unsigned i = 0; i < 100; ++i) Check(state.Update(true, true, true) == Transition::None);
	Check(state.Update(true, false, false) == Transition::None); // closing popup while our header retains focus
	Check(state.Update(false, false, false) == Transition::Leave);
	Check(state.Update(false, false, false) == Transition::None);
	Check(state.Update(false, true, false) == Transition::None); // mouse alone does not own pad/native focus
	Check(state.Update(false, true, true) == Transition::Enter); // pad takes a mouse-opened popup
	Check(state.Update(false, true, true) == Transition::None);
	Check(state.Update(false, false, false) == Transition::Leave);
	Check(state.Update(false, false, true) == Transition::None); // stale popup flag cannot suppress native focus
	Check(state.Update(true, false, false) == Transition::Enter);
	state.Reset();
	Check(state.Update(false, false, false) == Transition::None);
	Check(state.Update(true, false, false) == Transition::Enter);
	Check(state.Update(false, true, true) == Transition::None); // continuous ownership during handoff
	Check(state.Update(false, true, false) == Transition::Leave);
	for (unsigned mask = 0; mask < 8; ++mask) {
		state.Reset();
		const bool header = (mask & 1) != 0, open = (mask & 2) != 0, controller = (mask & 4) != 0;
		const bool owns = header || (open && controller);
		Check(state.Update(header, open, controller) == (owns ? Transition::Enter : Transition::None));
		Check(state.Update(header, open, controller) == Transition::None);
		Check(state.Update(false, false, false) == (owns ? Transition::Leave : Transition::None));
	}
	for (float original : { 0.0f, 0.25f, 0.7f, 1.0f }) {
		OpacitySnapshot tile;
		Check(!tile.HasValue());
		Check(tile.Capture(original));
		Check(tile.HasValue() && tile.Original() == original);
		Check(!tile.Capture(0.0f)); // second custom picker sees masked zero; must NOT replace backup
		Check(tile.Original() == original);
		Check(!tile.Capture(1.0f));
		Check(tile.Original() == original);
		tile.Reset();
		Check(!tile.HasValue());
		Check(tile.Capture(0.4f) && tile.Original() == 0.4f);
	}
	NativeHandoff shared;
	OpacitySnapshot background;
	Check(shared.Update(true, false, false) == Transition::Enter);
	Check(background.Capture(0.7f));
	// School -> Effect and Effect -> School share one owner union and one original opacity.
	for (unsigned owners : { 1u, 2u, 3u, 2u, 1u }) {
		Check(shared.Update(owners != 0, false, false) == Transition::None);
		Check(!background.Capture(0.0f) && background.Original() == 0.7f);
	}
	Check(shared.Update(false, false, false) == Transition::Leave);
	Check(background.Original() == 0.7f); // exact restoration value, not a guessed1 or captured0
	Check(!Owns(false, true, false));
	Check(Owns(false, true, true));
	Check(Owns(true, false, false));
	std::printf("%u picker highlight checks passed\n", checks);
}
