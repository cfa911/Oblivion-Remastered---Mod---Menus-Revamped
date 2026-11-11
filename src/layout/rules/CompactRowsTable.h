#pragma once
// Compact rows (owner 2026-11-11, [Layout] bCompactRows): the layout numbers of FuzzUI - Interface Tweaks 2.0 (Nexus 292), set at
// run time on the game's own widgets so its 27 replacement blueprints are not needed. Facts only, from a read-only diff of its
// cooked exports against game 1.512.105.0 (KB docs/reverse-engineering/fuzzui-interface-tweaks.md); no FuzzUI file is used.
// The exports carry no property names, so a change names the widget (or slot) and the documented stock value: it is written
// only where that many fields of the widget still hold the stock value. Header-only, no engine types (unit-tested).
#include <cmath>

namespace compactrows
{
	enum class Kind
	{
		Box,    // SizeBox: WidthOverride, HeightOverride
		Slot,   // a panel slot: CanvasPanelSlot LayoutData.Offsets, any other slot's Padding (FMargin, 4 floats)
		Font,   // a text block's Font.Size (a user widget wrapping one: its "text_block")
		Spacer  // a Spacer's Size (FVector2D, doubles): X, Y
	};
	enum class Group { Magic, Inventory, Crafting, StatsQuest };
	enum class Fit
	{
		Applied,          // the stock fields now hold the compact value
		AlreadyCompact,   // no stock field left, the compact value is there: an earlier pass did it
		Ambiguous,        // more stock fields than the change names: which one FuzzUI changed is unknown, nothing written
		Missing           // neither value: another mod's layout (or a game update), left alone
	};
	struct Change
	{
		const char* widget;   // the widget's or slot's object name in the blueprint
		Kind        kind;
		float       stock;
		float       compact;
		int         count;    // how many fields of the widget hold the stock value (both sides of a square icon: 2)
		int         field = -1;   // >= 0: only this field (FMargin 0 Left, 1 Top, 2 Right, 3 Bottom), for stock values shared by all
		int         guardField = -1;   // >= 0: only where this field holds guardValue (same-named slots told apart)
		float       guardValue = 0.0f;
	};
	struct Blueprint
	{
		const wchar_t* classPath;
		const char*    name;
		Group          group;
		const Change*  changes;
		int            changeCount;
	};

	inline constexpr float kTolerance = 0.01f;   // the diff printed 6 digits (485.447)
	inline bool Near(float a, float b) { return std::fabs(a - b) < kTolerance; }

	inline Fit Retarget(float* a_values, int a_count, const Change& a_change)
	{
		if (a_change.guardField >= 0 && (a_change.guardField >= a_count || !Near(a_values[a_change.guardField], a_change.guardValue)))
			return Fit::Missing;   // a same-named slot that is not the one meant
		if (a_change.field >= 0) {   // a named field: no value search
			if (a_change.field >= a_count) return Fit::Missing;
			float& v = a_values[a_change.field];
			if (Near(v, a_change.stock)) { v = a_change.compact; return Fit::Applied; }
			return Near(v, a_change.compact) ? Fit::AlreadyCompact : Fit::Missing;
		}
		int stock = 0, compact = 0;
		for (int i = 0; i < a_count; ++i) {
			if (Near(a_values[i], a_change.stock)) ++stock;
			else if (Near(a_values[i], a_change.compact)) ++compact;
		}
		if (stock == 0) return compact > 0 ? Fit::AlreadyCompact : Fit::Missing;
		if (stock > a_change.count) return Fit::Ambiguous;
		for (int i = 0; i < a_count; ++i)
			if (Near(a_values[i], a_change.stock)) a_values[i] = a_change.compact;
		return Fit::Applied;
	}

	// A SizeBox keeps its sizes in three pairs (overrides, min desired, max desired). The change goes to the first pair that still
	// holds the stock value; a pair already compact does not stop the search (Stats SizeBox_1: 54 in the overrides, 64 in another
	// pair, both -> 48; owner round 4, 2026-11-11).
	inline Fit RetargetPairs(float (*a_pairs)[2], int a_pairCount, const Change& a_change)
	{
		bool compact = false;
		for (int p = 0; p < a_pairCount; ++p) {
			const Fit fit = Retarget(a_pairs[p], 2, a_change);
			if (fit == Fit::Applied || fit == Fit::Ambiguous) return fit;
			compact = compact || fit == Fit::AlreadyCompact;
		}
		return compact ? Fit::AlreadyCompact : Fit::Missing;
	}

	// Not reproduced (judged by the owner's screenshots): double-valued alignment/anchor nudges (0.5 -> 0.55, 1.0 -> 1.1/1.2),
	// spacer sizes outside the Inventory main part, slots that only gained a property, Entry_Icon render values, IngredientDetails grid padding (5 -> 45 and 5 -> 0
	// on two of four equal sides).
	namespace detail
	{
		inline constexpr Change kMagicEntry[]{
			{ "SizeBox_0", Kind::Box, 54.0f, 36.0f, 1 },
			{ "SizeBox_1", Kind::Box, 48.0f, 32.0f, 2 },
		};
		inline constexpr Change kMagicMenu[]{
			{ "CanvasPanelSlot_1", Kind::Slot, 550.0f, 500.0f, 1 },
			{ "CanvasPanelSlot_2", Kind::Slot, 909.0f, 900.0f, 1 },
			{ "CanvasPanelSlot_4", Kind::Slot, -282.829f, -289.829f, 1 },
			{ "CanvasPanelSlot_4", Kind::Slot, 688.74f, 702.74f, 1 },
			{ "SizeBox_2", Kind::Box, 206.0f, 207.0f, 1 },
			{ "VerticalBoxSlot_0", Kind::Slot, 20.0f, 0.0f, 1 },   // SpellEffectiveness_Box (the header row's slot has the same name)
			// ours (owner 2026-11-11, "Magic like the Inventory"): VerticalBox_0 (bars + effectiveness line) padding (0,20,0,30)
			{ "VerticalBoxSlot_3", Kind::Slot, 20.0f, 4.0f, 1 },
			{ "VerticalBoxSlot_3", Kind::Slot, 30.0f, 10.0f, 1 },
			{ "Magic_SpellEffectivenes_Value", Kind::Font, 21.0f, 18.0f, 1 },
		};
		inline constexpr Change kInventoryIconEntry[]{
			{ "SizeBox_60", Kind::Box, 48.0f, 32.0f, 2 },
			{ "inv_entry_icon_count", Kind::Font, 12.0f, 11.0f, 1 },
		};
		inline constexpr Change kInventoryEntry[]{
			{ "SizeBox_0", Kind::Box, 54.0f, 36.0f, 1 },
		};
		inline constexpr Change kInventoryMainPart[]{
			{ "CanvasPanelSlot_2", Kind::Slot, 138.0f, 131.0f, 1 },
			{ "CanvasPanelSlot_2", Kind::Slot, 52.0f, 22.0f, 1 },
			{ "inv_armor", Kind::Font, 16.5f, 14.0f, 1 },
			{ "inv_money", Kind::Font, 16.5f, 14.0f, 1 },
			{ "inv_weight", Kind::Font, 16.5f, 14.0f, 1 },
			{ "Spacer_2", Kind::Spacer, 21.0f, 0.0f, 1 },   // the gaps above the bars and footer (export doubles, decoded 2026-11-11)
			{ "Spacer_3", Kind::Spacer, 33.0f, 0.0f, 1 },
		};
		// Alchemy follows FuzzUI Plus Alchemy (Nexus 1311 v1.5.1), not Interface Tweaks: Plus wins where both replace a blueprint
		// (owner's reference screenshot, rows ~27 units) and the owner chose it (2026-11-11). Its filter button and ingredient
		// details are stock. Not reproduced: the icon slot's anchor 0.5 -> 0.36 and the name slot's 1.0 -> 0.8 (doubles), the quantity
		// colour, and the properties some slots gain.
		inline constexpr Change kAlchemyItemEntry[]{
			{ "SizeBox_0", Kind::Box, 54.0f, 27.0f, 1 },
			{ "Icon_SizeBox", Kind::Box, 48.0f, 26.0f, 2 },
			{ "CanvasPanelSlot_4", Kind::Slot, 2.0f, 26.0f, 1 },    // icon slot offsets (2,0,48,48) -> (26,0,26,26)
			{ "CanvasPanelSlot_4", Kind::Slot, 48.0f, 26.0f, 2 },
			{ "CanvasPanelSlot_1", Kind::Slot, 20.0f, 18.0f, 1 },   // name slot (84,0.26,62,20): height 20 -> 18, x stays 84
		};
		inline constexpr Change kAlchemyMenu[]{
			// Alchemy_IngredientsListBG (the ingredient page parchment, T_Spellmaking_LeftPaperBackground stretched), owner
			// 2026-11-11: from the screen top (where the old Alchemy-TopParchment strip began, 93.7 above the stock top) down to the
			// owner's mark (bottom 980.2); the active-filter buttons use the space it adds at the top
			{ "CanvasPanelSlot_0", Kind::Slot, 60.7868f, -32.9132f, 1 },   // top
			{ "CanvasPanelSlot_0", Kind::Slot, 809.418f, 1013.118f, 1 },   // height
			{ "CanvasPanelSlot_1", Kind::Slot, 712.043f, 812.043f, 1 },
			{ "OverlaySlot_2", Kind::Slot, 570.0f, 665.0f, 1 },   // IngredientDetailsize (the effects block) top padding
		};
		inline constexpr Change kAlchemyFilters[]{
			{ "CanvasPanelSlot_0", Kind::Slot, 744.831f, 755.831f, 1 },
			{ "CanvasPanelSlot_2", Kind::Slot, 64.0f, 30.0f, 1 },
			// ListView_Filters' slot (padding 10,0,10,0; other OverlaySlot_0 read 0,0,0,0): bottom -75, the list reaches lower
			{ "OverlaySlot_0", Kind::Slot, 0.0f, -75.0f, 1, 3, 0, 10.0f },
		};
		inline constexpr Change kFilterEffectEntry[]{
			{ "SizeBox", Kind::Box, 58.0f, 34.0f, 1 },
			{ "EffectText", Kind::Font, 20.0f, 16.0f, 1 },
			{ "OverlaySlot_0", Kind::Slot, 5.0f, 2.0f, 4 },
			{ "OverlaySlot_4", Kind::Slot, 5.0f, 2.0f, 4 },
			{ "OverlaySlot_5", Kind::Slot, 5.0f, 2.0f, 4 },
		};
		inline constexpr Change kAlchemyItemsListView[]{   // stretched slot: stock margins 0; Plus extends the list 81 down
			{ "CanvasPanelSlot_0", Kind::Slot, 0.0f, -5.0f, 1, 2 },    // Right (export floats -5, -81 with the zero fields omitted)
			{ "CanvasPanelSlot_0", Kind::Slot, 0.0f, -81.0f, 1, 3 },   // Bottom
		};
		inline constexpr Change kItemEffect[]{   // the ingredient's effect rows under the list
			{ "EffectText", Kind::Font, 18.0f, 22.0f, 1 },
		};
		inline constexpr Change kEnchantmentEntry[]{
			{ "SizeBox_0", Kind::Box, 54.0f, 36.0f, 1 },
		};
		inline constexpr Change kEnchantmentMenu[]{
			{ "ListView_SizeBox", Kind::Box, 540.0f, 590.0f, 1 },
		};
		inline constexpr Change kSpellmakingEntry[]{
			{ "SizeBox_0", Kind::Box, 54.0f, 36.0f, 1 },
		};
		inline constexpr Change kSpellMakingMenu[]{
			{ "CanvasPanelSlot_1", Kind::Slot, 709.684f, 735.0f, 1 },
		};
		inline constexpr Change kSpellPurchaseItem[]{
			{ "SizeBox_0", Kind::Box, 54.0f, 36.0f, 1 },
			{ "effect_icon_sizebox", Kind::Box, 48.0f, 32.0f, 2 },
		};
		inline constexpr Change kSpellPurchase[]{
			{ "CanvasPanelSlot_5", Kind::Slot, 320.0f, 355.0f, 1 },
		};
		inline constexpr Change kStatsSkillEntry[]{
			{ "CanvasPanelSlot_5", Kind::Slot, 54.0f, 36.0f, 1 },
			{ "SizeBox_1", Kind::Box, 54.0f, 48.0f, 1 },
			{ "SizeBox_1", Kind::Box, 64.0f, 48.0f, 1 },
			{ "stat_skillentry_icon_sizebox", Kind::Box, 64.0f, 48.0f, 2 },
			{ "stat_skillentry_label", Kind::Font, 25.5f, 22.5f, 1 },
			{ "stat_skillentry_value", Kind::Font, 18.0f, 16.0f, 1 },
		};
		inline constexpr Change kStatsLevelDescription[]{
			{ "stat_level_description", Kind::Font, 18.0f, 16.0f, 1 },
			{ "stat_level_name", Kind::Font, 25.5f, 23.5f, 1 },
			{ "stat_level_value", Kind::Font, 18.0f, 16.0f, 1 },
		};
		inline constexpr Change kStatsPage3[]{
			{ "CanvasPanelSlot_6", Kind::Slot, -21.798f, 0.798018f, 1 },
			{ "CanvasPanelSlot_6", Kind::Slot, 659.944f, 710.944f, 1 },
		};
		inline constexpr Change kStatsPage3Details[]{
			{ "stat_detailsp3_skillbase", Kind::Font, 21.0f, 18.0f, 1 },
		};
		inline constexpr Change kQuestEntry[]{
			{ "SizeBox_0", Kind::Box, 54.0f, 36.0f, 1 },
			{ "CanvasPanelSlot_4", Kind::Slot, 48.0f, 32.0f, 2 },
		};
		template <class T, int N>
		constexpr int Count(const T (&)[N]) { return N; }
	}

#define MR_COMPACT_BP(folder, name, group, table) \
	Blueprint{ L"/Game/UI/" folder L"/" L## #name L"." L## #name L"_C", #name, group, detail::table, detail::Count(detail::table) }

	inline constexpr Blueprint kBlueprints[]{
		MR_COMPACT_BP(L"Modern/GameMenuLayer/Magic", WBP_ModernMenu_MagicEntry, Group::Magic, kMagicEntry),
		MR_COMPACT_BP(L"Modern/GameMenuLayer/Magic", WBP_ModernMenu_MagicMenu, Group::Magic, kMagicMenu),
		MR_COMPACT_BP(L"Original/GameMenuLayer/Inventory/MainPart", WBP_OriginalMenu_InventoryIconEntry, Group::Inventory, kInventoryIconEntry),
		MR_COMPACT_BP(L"Original/GameMenuLayer/Inventory/MainPart", WBP_OriginalMenu_InventoryEntry, Group::Inventory, kInventoryEntry),
		MR_COMPACT_BP(L"Original/GameMenuLayer/Inventory/MainPart", WBP_OriginalMenu_InventoryMainPart, Group::Inventory, kInventoryMainPart),
		MR_COMPACT_BP(L"Modern/GameMenuLayer/Alchemy", WBP_ModernMenu_AlchemyItemEntry, Group::Crafting, kAlchemyItemEntry),
		MR_COMPACT_BP(L"Modern/GameMenuLayer/Alchemy", WBP_ModernMenu_AlchemyMenu, Group::Crafting, kAlchemyMenu),
		MR_COMPACT_BP(L"Modern/GameMenuLayer/Alchemy/Filters", WBP_ModernMenu_AlchemyFilters, Group::Crafting, kAlchemyFilters),
		MR_COMPACT_BP(L"Modern/GameMenuLayer/Alchemy/Filters", WBP_ModernMenu_FilterEffectEntry, Group::Crafting, kFilterEffectEntry),
		MR_COMPACT_BP(L"Modern/GameMenuLayer/Alchemy", WBP_ModernMenu_ItemEffect, Group::Crafting, kItemEffect),
		MR_COMPACT_BP(L"Modern/GameMenuLayer/Alchemy", WBP_ModernMenu_AlchemyItemsListView, Group::Crafting, kAlchemyItemsListView),
		MR_COMPACT_BP(L"Modern/GameMenuLayer/Enchantment", WBP_ModernMenu_EnchantmentEntry, Group::Crafting, kEnchantmentEntry),
		MR_COMPACT_BP(L"Modern/GameMenuLayer/Enchantment", WBP_ModernMenu_EnchantmentMenu, Group::Crafting, kEnchantmentMenu),
		MR_COMPACT_BP(L"Modern/GameMenuLayer/SpellMaking", WBP_ModernMenu_SpellmakingEntry, Group::Crafting, kSpellmakingEntry),
		MR_COMPACT_BP(L"Modern/GameMenuLayer/SpellMaking", WBP_ModernMenu_SpellMakingMenu, Group::Crafting, kSpellMakingMenu),
		MR_COMPACT_BP(L"Modern/GameMenuLayer/SpellPurchase", WBP_ModernMenu_SpellPurchaseItem, Group::Crafting, kSpellPurchaseItem),
		MR_COMPACT_BP(L"Modern/GameMenuLayer/SpellPurchase", WBP_ModernMenu_SpellPurchase, Group::Crafting, kSpellPurchase),
		MR_COMPACT_BP(L"Modern/GameMenuLayer/Stats/Page3_Skills", WBP_ModernMenu_StatsSkillEntry, Group::StatsQuest, kStatsSkillEntry),
		MR_COMPACT_BP(L"Modern/GameMenuLayer/Stats/Page3_Skills", WBP_ModernMenu_Stats_LevelDescription, Group::StatsQuest, kStatsLevelDescription),
		MR_COMPACT_BP(L"Modern/GameMenuLayer/Stats/Page3_Skills", WBP_ModernMenu_StatsPage3, Group::StatsQuest, kStatsPage3),
		MR_COMPACT_BP(L"Modern/GameMenuLayer/Stats/Page3_Skills", WBP_ModernMenu_StatsPage3_Details, Group::StatsQuest, kStatsPage3Details),
		MR_COMPACT_BP(L"Original/GameMenuLayer/Quest/Prefabs", WBP_Modern_QuestEntry, Group::StatsQuest, kQuestEntry),
	};
#undef MR_COMPACT_BP
	inline constexpr int kBlueprintCount = detail::Count(kBlueprints);
}
