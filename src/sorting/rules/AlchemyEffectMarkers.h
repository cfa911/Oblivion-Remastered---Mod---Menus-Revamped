#pragma once

#include "AlchemyRules.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace alchemyrules
{
	inline constexpr int kMarkerLimit = 12, kIngredientMarkerLimit = 4;
	inline constexpr double kMarkerGap = 4, kMarkerBeforeWeight = 28;
	inline double g_markerDiameter = 18;   // [Alchemy] uMarkerDiameter
	// Shared chip/ingredient palette, in selected-effect order (0xRRGGBB, sRGB). Solid colors, not game effect IDs.
	inline constexpr std::array<std::uint32_t, kMarkerLimit> kDefaultMarkerPalette = {
		0x316EE0, 0xDC3730, 0x48AE37, 0xEFCF2F, 0x9740C3, 0xED8623, 0x27C0CC, 0xE05AA0,
		0x607D24, 0x91512C, 0x545F91, 0xF2A8A1
	};
	inline std::array<std::uint32_t, kMarkerLimit> g_markerPalette = kDefaultMarkerPalette;   // [Alchemy] sMarkerColors

	// "#DC3730,#48AE37,...": 8 legacy or 12 hex colours, '#' optional, spaces ignored. False leaves a_out unchanged.
	inline bool ParseMarkerPalette(std::string_view a_text, std::array<std::uint32_t, kMarkerLimit>& a_out)
	{
		std::array<std::uint32_t, kMarkerLimit> parsed = kDefaultMarkerPalette;
		std::size_t count = 0;
		while (true) {
			const auto comma = a_text.find(',');
			auto item = a_text.substr(0, comma);
			while (!item.empty() && (item.front() == ' ' || item.front() == '\t' || item.front() == '#')) item.remove_prefix(1);
			while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) item.remove_suffix(1);
			if (item.size() != 6 || count >= kMarkerLimit) return false;
			std::uint32_t value = 0;
			for (const char c : item) {
				const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
				if (digit < 0) return false;
				value = value * 16 + static_cast<std::uint32_t>(digit);
			}
			parsed[count++] = value;
			if (comma == std::string_view::npos) break;
			a_text.remove_prefix(comma + 1);
		}
		if (count != 8 && count != kMarkerLimit) return false;
		a_out = parsed;
		return true;
	}

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
		const auto c = g_markerPalette[static_cast<std::size_t>(std::clamp(a_index, 0, kMarkerLimit - 1))];
		return { MarkerLinear(((c >> 16) & 0xFF) / 255.0f), MarkerLinear(((c >> 8) & 0xFF) / 255.0f), MarkerLinear((c & 0xFF) / 255.0f), 1 };
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
		return -a_weightFromRight - kMarkerBeforeWeight - a_count * g_markerDiameter - (a_count - 1) * kMarkerGap + a_index * (g_markerDiameter + kMarkerGap);
	}
}
