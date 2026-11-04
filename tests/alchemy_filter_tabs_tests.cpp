#include <vector>
#include <cstdio>
#if __has_include("../src/sorting/rules/AlchemyFilterTabs.h")
#include "../src/sorting/rules/AlchemyFilterTabs.h"
#else
// Baseline adapter: current menu exposes Positive/Negative only, selecting
// the current native list. This keeps the pre-feature run behavioral.
namespace alchemytabs {
enum class Tab { All, Positive, Negative };
inline Tab Cycle(Tab t, int) { return t == Tab::Positive ? Tab::Negative : Tab::Positive; }
template<class T> std::vector<T> Items(Tab t, const std::vector<T>& p, const std::vector<T>& n) { return t == Tab::Negative ? n : p; }
}
#endif
int main()
{
	using namespace alchemytabs;
	int failures = 0, checks = 0;
	auto check = [&](bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", why); } };
	const std::vector<int> p{1, 2}, n{3, 4};
	check(Items(Tab::All, p, n) == std::vector<int>({1,2,3,4}), "All preserves both kinds and their order");
	check(Items(Tab::Positive, p, n) == p, "Positive excludes Negative");
	check(Items(Tab::Negative, p, n) == n, "Negative excludes Positive");
	check(Items(Tab::All, std::vector<int>{}, n) == n, "All supports empty Positive");
	check(Items(Tab::All, p, std::vector<int>{}) == p, "All supports empty Negative");
	check(Cycle(Tab::All, 1) == Tab::Positive, "All -> Positive");
	check(Cycle(Tab::Positive, 1) == Tab::Negative, "Positive -> Negative");
	check(Cycle(Tab::Negative, 1) == Tab::All, "Negative -> All");
	check(Cycle(Tab::All, -1) == Tab::Negative, "All backwards -> Negative");
	check(Cycle(Tab::Negative, -1) == Tab::Positive, "Negative backwards -> Positive");
	check(Cycle(Tab::Positive, -1) == Tab::All, "Positive backwards -> All");
	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
