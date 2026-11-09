#pragma once

#include <array>

namespace uistyle
{
	enum class Preset : int { Classic = 0, Light = 1, Dark = 2 };

	using Color = std::array<float, 4>;

	struct Search
	{
		Color fill, edge, focus, ink, hint, selectedInk, alchemyInk;
	};

	struct Picker
	{
		Color paper, border, ink, titleInk, focusFill;
		double titleFontSize, labelFontSize;
		float inactiveIconOpacity;
		float inactiveSortOpacity;
	};

	struct Alchemy
	{
		Color activeSort, focusTint, sortInk, chipInk, buttonFill, buttonHover, buttonLine, buttonLineDark, titleInk;
		double titleFontSize, chipFontSize;
		float zeroOpacity;
	};

	struct Palette
	{
		Search search;
		Picker picker;
		Alchemy alchemy;
	};

	inline constexpr Search kInventorySkinSearch{
		{0.025f,0.025f,0.025f,1}, {0.23f,0.23f,0.23f,1}, {0.58f,0.58f,0.58f,1},
		{0.88f,0.88f,0.88f,1}, {0.63f,0.63f,0.63f,1}, {1,1,1,1}, {1,1,1,1}
	};
	inline constexpr Picker kInventorySkinPicker{
		{0.035f,0.035f,0.035f,1}, {0.25f,0.25f,0.25f,1}, {0.9f,0.9f,0.9f,1},
		{0.96f,0.96f,0.96f,1}, {0.24f,0.24f,0.24f,0.85f}, 18.0, 14.0, 0.72f, 0.72f
	};

	inline constexpr Palette kClassic{
		{
			{ 0.16f, 0.09f, 0.04f, 1.0f }, { 0.06f, 0.035f, 0.018f, 1.0f }, { 0.78f, 0.62f, 0.28f, 1.0f },
			{ 0.93f, 0.90f, 0.82f, 1.0f }, { 0.58f, 0.53f, 0.45f, 1.0f }, { 0.78f, 0.62f, 0.28f, 1.0f },
			{ 0.0585f, 0.0313f, 0.0235f, 1.0f }
		},
		{
			{ 0.672443f, 0.630757f, 0.577580f, 1.0f }, { 0.20f, 0.14f, 0.09f, 1.0f },
			{ 0.20f, 0.14f, 0.09f, 1.0f }, { 0.06f, 0.035f, 0.016f, 1.0f },
			{ 0.7912979403f, 0.7758222183f, 0.7379104088f, 1.0f }, 18.0, 14.0, 0.45f, 0.5f
		},
		{
			{ 0.55f, 0.35f, 0.20f, 1.0f }, { 1.0f, 1.0f, 1.0f, 0.55f },
			{ 0.035f, 0.025f, 0.015f, 1.0f }, { 0.30f, 0.012f, 0.008f, 1.0f },
			{ 0.30f, 0.24f, 0.16f, 0.30f }, { 0.62f, 0.50f, 0.30f, 0.65f },
			{ 0.30f, 0.012f, 0.008f, 1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f },
			{ 0.0723f, 0.0382f, 0.0284f, 1.0f }, 19.0, 13.0, 0.4f
		}
	};

	inline constexpr Palette kLight{
		{
			{ 0.82f, 0.77f, 0.68f, 1.0f }, { 0.24f, 0.17f, 0.10f, 1.0f }, { 0.78f, 0.56f, 0.22f, 1.0f },
			{ 0.12f, 0.08f, 0.045f, 1.0f }, { 0.34f, 0.27f, 0.19f, 1.0f }, { 0.40f, 0.22f, 0.06f, 1.0f },
			{ 0.0585f, 0.0313f, 0.0235f, 1.0f }
		},
		{
			{ 0.82f, 0.77f, 0.68f, 1.0f }, { 0.24f, 0.17f, 0.10f, 1.0f },
			{ 0.16f, 0.11f, 0.06f, 1.0f }, { 0.07f, 0.04f, 0.02f, 1.0f },
			{ 0.90f, 0.77f, 0.50f, 0.85f }, 18.0, 14.0, 0.45f, 0.5f
		},
		{
			{ 0.55f, 0.35f, 0.20f, 1.0f }, { 1.0f, 1.0f, 1.0f, 0.55f },
			{ 0.035f, 0.025f, 0.015f, 1.0f }, { 0.30f, 0.012f, 0.008f, 1.0f },
			{ 0.30f, 0.24f, 0.16f, 0.30f }, { 0.62f, 0.50f, 0.30f, 0.65f },
			{ 0.30f, 0.012f, 0.008f, 1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f },
			{ 0.0723f, 0.0382f, 0.0284f, 1.0f }, 19.0, 13.0, 0.4f
		}
	};

	inline constexpr Palette kDark{
		{
			{ 0.035f, 0.030f, 0.025f, 1.0f }, { 0.34f, 0.26f, 0.15f, 1.0f }, { 0.82f, 0.58f, 0.22f, 1.0f },
			{ 0.94f, 0.89f, 0.77f, 1.0f }, { 0.60f, 0.54f, 0.43f, 1.0f }, { 0.98f, 0.70f, 0.25f, 1.0f },
			{ 0.90f, 0.83f, 0.69f, 1.0f }
		},
		{
			{ 0.045f, 0.040f, 0.034f, 1.0f }, { 0.38f, 0.29f, 0.17f, 1.0f },
			{ 0.91f, 0.85f, 0.73f, 1.0f }, { 0.99f, 0.79f, 0.48f, 1.0f },
			{ 0.62f, 0.43f, 0.20f, 0.75f }, 18.0, 14.0, 0.52f, 0.5f
		},
		{
			{ 0.94f, 0.62f, 0.25f, 1.0f }, { 0.95f, 0.79f, 0.52f, 0.60f },
			{ 0.91f, 0.85f, 0.73f, 1.0f }, { 0.95f, 0.30f, 0.17f, 1.0f },
			{ 0.12f, 0.09f, 0.06f, 0.92f }, { 0.42f, 0.27f, 0.11f, 0.95f },
			{ 0.84f, 0.48f, 0.20f, 1.0f }, { 0.38f, 0.27f, 0.14f, 1.0f },
			{ 0.94f, 0.86f, 0.72f, 1.0f }, 19.0, 13.0, 0.52f
		}
	};

	inline constexpr const Palette& Get(Preset a_preset)
	{
		switch (a_preset) {
		case Preset::Light: return kLight;
		case Preset::Dark: return kDark;
		default: return kClassic;
		}
	}
}
