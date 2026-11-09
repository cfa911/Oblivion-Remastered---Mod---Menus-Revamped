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

	// The native menu skin (Inventory now; container and barter reuse it). Colours and art only: the passes that apply it
	// (sorting/MenuSkin.cpp) and every layout change run the same in all presets. recolor=false keeps the game's colours.
#define UISTYLE_SKIN 1
#define UISTYLE_SKIN_ART 1
#define UISTYLE_SKIN_EMPHASIS 1
	// a stock texture drawn instead from a PNG in MenusRevamped/Skin (per widget, inside the skinned menu only; the stock
	// texture itself stays untouched for every other menu). Brown baked into art cannot be fixed by a tint.
	struct ArtSwap
	{
		const char*    stock;
		const wchar_t* file;
	};

	struct Skin
	{
		bool           recolor, invertToggles, hideEquippedScribble;
		Color          ink, perkInk, rule, guide;
		const ArtSwap* artSwaps;          // made by tools/make_dark_skin_art.py from the stock textures (stock alpha kept)
		int            artSwapCount;
		Color          ring;              // the skill level ring (M_UI_CircularProgess "Color")
		float          pageGray;          // the details page tint (M_MaskedPage keeps its shape)
		Color          ornament, pattern, band, buttonFill;
		float          scrollTrack, scrollBorder, scrollInside;
		Color          emphasisInk;       // brown rich-text emphasis rows (perk descriptions); alpha 0 = the table's own
		Color          buttonRule;        // the action button's colour-only rule middles (brush tint); alpha 0 = stock
	};

	inline constexpr ArtSwap kDarkArt[]{
		{ "T_List_Default", L"RowSeparator.png" },        // row separator line: white
		{ "T_List_equip_V2", L"RowEquipped.png" },        // equipped row: gold band (RMI-072; owner: stronger, gold)
		{ "T_Property_1_magnitude", L"StatMagnitude.png" },
		{ "T_Property_1_Range", L"StatRange.png" },
		{ "T_Property_1_time", L"StatTime.png" },
		{ "T_Property_1_cost", L"StatCost.png" },
		{ "T_inv_details_effects_title_BG", L"TitleBand.png" },
		{ "T_SigilStone_SubNameBG", L"SigilBand.png" },
		{ "T_Category_Armor", L"SigilArmor.png" },
		{ "T_Category_Weapons", L"SigilWeapons.png" },
		{ "T_CircularProgress_bg", L"RingCentre.png" },
		{ "T_Arrow_Empty", L"ButtonArrow.png" },
		{ "T_Button_State_Hovered_LeftLineFade", L"ButtonLine.png" },
	};

	inline constexpr Skin kStockSkin{
		false, false, false,
		{ 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0.20f, 0.14f, 0.09f, 0.55f }, { 0.20f, 0.14f, 0.09f, 0.13f },
		nullptr, 0, { 1, 1, 1, 1 }, 1.0f,
		{ 1, 1, 1, 1 }, { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, { 1, 1, 1, 1 },
		0, 0, 0,
		{ 0, 0, 0, 0 },
		{ 0, 0, 0, 0 }
	};
	inline constexpr Skin kDarkSkin{
		true, true, true,
		{ 0.86f, 0.86f, 0.86f, 1 }, { 0.86f, 0.86f, 0.86f, 1 }, { 0.65f, 0.65f, 0.65f, 0.55f }, { 0.55f, 0.55f, 0.55f, 0.13f },
		kDarkArt, int(sizeof(kDarkArt) / sizeof(kDarkArt[0])), { 0.55f, 0.55f, 0.55f, 1 },
		0.010f,                          // sRGB about 25 (the list is 41); black hides the parchment completely
		{ 0.45f, 0.45f, 0.45f, 1 },      // T_StatsMenu_Line_R (white art)
		{ 1, 1, 1, 1 },                  // T_Inventory_pattern ships gray in the texture package
		{ 0.41f, 0.65f, 1.0f, 1 },       // per-channel inverse of T_inv_details_effects_title_BG's baked brown
		{ 0.12f, 0.12f, 0.12f, 1 },      // action button fill and arrows
		0.0052f, 0.0052f, 0.0222f,       // scrollbar track, thumb border, thumb inside (linear gray)
		{ 0.125f, 0.120f, 0.125f, 1 },   // owner swatch: the highlighted sub-tab disc gray (sRGB 99 97 99)
		{ 0.83f, 0.83f, 0.83f, 1 }       // rule middles match the end pieces (ButtonLine.png, sRGB 235)
	};

	inline constexpr const Skin& SkinFor(Preset a_preset) { return a_preset == Preset::Dark ? kDarkSkin : kStockSkin; }

	inline constexpr const Palette& Get(Preset a_preset)
	{
		switch (a_preset) {
		case Preset::Light: return kLight;
		case Preset::Dark: return kDark;
		default: return kClassic;
		}
	}
}
