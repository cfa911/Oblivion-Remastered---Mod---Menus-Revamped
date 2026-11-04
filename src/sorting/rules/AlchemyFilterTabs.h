#pragma once
#include <vector>
namespace alchemytabs
{
	enum class Tab { All, Positive, Negative };
	inline Tab Cycle(Tab a_tab, int a_direction)
	{
		return static_cast<Tab>((static_cast<int>(a_tab) + (a_direction < 0 ? 2 : 1)) % 3);
	}
	template<class T>
	std::vector<T> Items(Tab a_tab, const std::vector<T>& a_positive, const std::vector<T>& a_negative)
	{
		if (a_tab == Tab::Positive) return a_positive;
		if (a_tab == Tab::Negative) return a_negative;
		auto out = a_positive;
		out.insert(out.end(), a_negative.begin(), a_negative.end());
		return out;
	}
}
