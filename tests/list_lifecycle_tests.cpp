#include "../src/search/ListLifecycle.h"
#include <iostream>

int main()
{
	int checks = 0, failed = 0;
	auto check = [&](bool ok, const char* why) {
		++checks; if (!ok) { ++failed; std::cerr << "FAIL " << why << '\n'; }
	};
	revamped::ListLifecycle<int> yours, theirs;
	check(yours.Observe({ 1, 2, 3 }), "initial complete source captured");
	yours.Commit({ 2 });
	check(!yours.Observe({ 2 }), "filtered view is not a new native source");
	check(yours.source == std::vector<int>({ 1, 2, 3 }), "clear search restores hidden source rows");
	yours.Commit({});
	check(!yours.Observe({}), "zero matches cannot erase complete source");
	check(yours.source.size() == 3, "zero-match source retained");
	const auto generation = yours.generation;
	yours.nativeDirty = true;
	check(yours.Observe({}), "actual empty native rebuild replaces old source");
	check(yours.source.empty() && yours.generation == generation + 1, "empty native generation advances");
	yours.Observe({ 4, 5 }); yours.Commit({ 4, 5 });
	const auto reused = yours.generation;
	yours.nativeDirty = true;
	check(yours.Observe({ 4, 5 }) && yours.generation == reused + 1, "same-count reused wrappers require new classification");
	theirs.Observe({ 8, 9 }); theirs.Commit({ 8 });
	yours.Reset();
	check(theirs.source == std::vector<int>({ 8, 9 }), "resetting player does not narrow merchant source");
	check(!yours.haveSource && yours.nativeDirty, "closed context requires fresh native source");
	check(!theirs.Observe({ 8 }), "query/category changes keep same merchant source");
	check(theirs.Observe({ 9, 10 }), "native replacement differs from applied result");
	std::cout << checks << " checks, " << failed << " failures\n";
	return failed ? 1 : 0;
}
