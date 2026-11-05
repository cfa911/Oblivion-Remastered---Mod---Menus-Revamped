#pragma once

#include "AlchemyRules.h"
#include <cmath>

namespace alchemyrules
{
	inline constexpr int kMarkerLimit = 8, kIngredientMarkerLimit = 4;
	inline constexpr double kMarkerDiameter = 14, kMarkerGap = 4, kMarkerBeforeWeight = 28;

	inline float MarkerLinear(float a_srgb)
	{
		return a_srgb <= 0.04045f ? a_srgb / 12.92f : std::pow((a_srgb + 0.055f) / 1.055f, 2.4f);
	}

	inline std::array<float, 4> MarkerOutline()
	{
		return { MarkerLinear(76.0f / 255), MarkerLinear(55.0f / 255), MarkerLinear(47.0f / 255), 1 };
	}

	inline std::array<float, 4> MarkerColor(int a_index)
	{
		// Shared chip/ingredient palette, in selected-effect order. Solid colors, not game effect IDs.
		constexpr unsigned char rgb[kMarkerLimit][3] = {
			{ 220, 55, 48 }, { 72, 174, 55 }, { 151, 64, 195 }, { 239, 207, 47 },
			{ 49, 110, 224 }, { 237, 134, 35 }, { 39, 192, 204 }, { 224, 90, 160 }
		};
		const auto& c = rgb[std::clamp(a_index, 0, kMarkerLimit - 1)];
		return { MarkerLinear(c[0] / 255.0f), MarkerLinear(c[1] / 255.0f), MarkerLinear(c[2] / 255.0f), 1 };
	}

	inline std::vector<int> IngredientMarkers(const std::vector<std::string>& a_known, const std::vector<std::string>& a_selected)
	{
		std::vector<int> result;
		std::unordered_set<std::string> emitted;
		for (int i = 0; i < kMarkerLimit && i < static_cast<int>(a_selected.size()) && result.size() < kIngredientMarkerLimit; ++i) {
			const auto key = MatchKey(a_selected[i]);
			if (key.empty() || emitted.count(key)) continue;
			if (std::any_of(a_known.begin(), a_known.end(), [&](const auto& effect) { return MatchKey(effect) == key; })) {
				result.push_back(i);
				emitted.insert(key);
			}
		}
		return result;
	}

	inline double IngredientMarkerX(int a_index, int a_count, double a_weightFromRight)
	{
		return -a_weightFromRight - kMarkerBeforeWeight - a_count * kMarkerDiameter - (a_count - 1) * kMarkerGap + a_index * (kMarkerDiameter + kMarkerGap);
	}
}
