#pragma once

#include <array>
#include <string_view>
#include "core/DarkStyle.h"

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
		const char*      stock;
		const wchar_t*   file;
		darkstyle::Value tint = nullptr;   // a white-shape PNG drawn in this DarkStyle.h colour; nullptr = the art's own colours
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
		// Magic row spell-type icons (brown art, shared with Sigil Stone and the effect lists, so not baked): white shapes drawn
		// in rowIcon (tools/make_dark_row_icons.py). The school icons, details stat icons and tabs are baked into MRDark_P.
		{ "T_MagicMenuIcon_Ability_ListSmall", L"MagicRow-T_MagicMenuIcon_Ability_ListSmall.png", &darkstyle::Style::rowIcon },
		{ "T_MagicMenuIcon_Disease_ListSmall", L"MagicRow-T_MagicMenuIcon_Disease_ListSmall.png", &darkstyle::Style::rowIcon },
		{ "T_MagicMenuIcon_Enchantment_ListSmall", L"MagicRow-T_MagicMenuIcon_Enchantment_ListSmall.png", &darkstyle::Style::rowIcon },
		{ "T_MagicMenuIcon_GreaterPower_ListSmall", L"MagicRow-T_MagicMenuIcon_GreaterPower_ListSmall.png", &darkstyle::Style::rowIcon },
		{ "T_MagicMenuIcon_LesserPower_ListSmall", L"MagicRow-T_MagicMenuIcon_LesserPower_ListSmall.png", &darkstyle::Style::rowIcon },
		{ "T_MagicMenuIcon_Poison_ListSmall", L"MagicRow-T_MagicMenuIcon_Poison_ListSmall.png", &darkstyle::Style::rowIcon },
		{ "T_MagicMenuIcon_Potion_ListSmall", L"MagicRow-T_MagicMenuIcon_Potion_ListSmall.png", &darkstyle::Style::rowIcon },
		{ "T_MagicMenuIcon_Scroll_ListSmall", L"MagicRow-T_MagicMenuIcon_Scroll_ListSmall.png", &darkstyle::Style::rowIcon },
		{ "T_MagicMenuIcon_SpellCustom_ListSmall", L"MagicRow-T_MagicMenuIcon_SpellCustom_ListSmall.png", &darkstyle::Style::rowIcon },
		{ "T_MagicMenuIcon_Spell_ListSmall", L"MagicRow-T_MagicMenuIcon_Spell_ListSmall.png", &darkstyle::Style::rowIcon },
		{ "T_MagicMenuIcon_WortCraft_ListSmall", L"MagicRow-T_MagicMenuIcon_WortCraft_ListSmall.png", &darkstyle::Style::rowIcon },
		{ "T_MagicBackground", L"MagicPage.png" },   // Magic page: the Inventory panel art's curve, baked (DarkStyle.h panel/frame)
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
		darkstyle::Arr(darkstyle::kDefaults.rowText), darkstyle::Arr(darkstyle::kDefaults.rowText),   // row and perk ink
		darkstyle::kDefaults.skin.rule, darkstyle::kDefaults.skin.guide,
		kDarkArt, int(sizeof(kDarkArt) / sizeof(kDarkArt[0])), darkstyle::kDefaults.skin.ring,
		darkstyle::kDefaults.detailsPage,   // the details page tint (Inventory and Magic)
		darkstyle::kDefaults.skin.ornament, darkstyle::kDefaults.skin.pattern, darkstyle::kDefaults.skin.band,
		darkstyle::kDefaults.skin.buttonFill,
		darkstyle::kDefaults.skin.scrollTrack, darkstyle::kDefaults.skin.scrollBorder, darkstyle::kDefaults.skin.scrollInside,
		darkstyle::kDefaults.skin.emphasisInk, darkstyle::kDefaults.skin.buttonRule
	};

	inline constexpr const Skin& SkinFor(Preset a_preset) { return a_preset == Preset::Dark ? kDarkSkin : kStockSkin; }

#define UISTYLE_INVENTORY_CONTROL_PAINT 1
	struct InventoryControlStyle
	{
		bool darkArt;
		float selectedSortTint;
	};

	inline constexpr InventoryControlStyle InventoryControls(bool inventory, Preset preset)
	{
		const bool dark = inventory && SkinFor(preset).recolor;
		return { dark, dark ? 0.24f : 1.0f };
	}

	inline constexpr const Palette& Get(Preset a_preset)
	{
		switch (a_preset) {
		case Preset::Light: return kLight;
		case Preset::Dark: return kDark;
		default: return kClassic;
		}
	}

	// Phase 2 (owner 2026-11-10): the Magic menu takes the Inventory's Dark look. Picker kinds the menu skin covers: 0 Inventory;
	// 1/2/5 the Magic School, Effect and Type pickers; 6 Magic Active Effects. Container sides (3/4) keep the preset palette.
#define UISTYLE_MAGIC_SKIN 1
	inline constexpr bool SkinnedKind(int kind) { return kind == 0 || kind == 1 || kind == 2 || kind == 5 || kind == 6; }
	inline constexpr const Search& SearchFor(bool skinnedMenu, Preset p)
	{
		return skinnedMenu && SkinFor(p).recolor ? kInventorySkinSearch : Get(p).search;
	}
	inline constexpr const Picker& PickerFor(bool skinnedMenu, Preset p)
	{
		return skinnedMenu && SkinFor(p).recolor ? kInventorySkinPicker : Get(p).picker;
	}
	// The Magic page (T_MagicBackground, shared with Quest/Help/Stats, so never replaced in a texture package) is swapped per
	// widget inside Magic only for MagicPage.png, its own shape in white, drawn in DarkStyle.h panel (the Inventory recipe).
	inline constexpr bool IsMagicPage(std::string_view resource) { return resource == "T_MagicBackground"; }
	// Round 2 (owner screenshot 2026-11-10): in Dark the Magic header icons (Effect, Type, Cost...) are full white like the
	// A-Z and School icons, not dimmed while inactive; the active one keeps its gray background and arrow.
#define UISTYLE_MAGIC_SKIN_R2 1
	inline constexpr float MagicHeaderOpacity(Preset p, float stock) { return SkinFor(p).recolor ? 1.0f : stock; }
}
