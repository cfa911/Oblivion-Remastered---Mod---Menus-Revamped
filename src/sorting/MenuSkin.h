#pragma once
// The native menu skin: the preset's skin and the menu targets. Every native colour and art of the Custom preset is baked
// in the Custom pack (decision 28; Magic rows, row icons, title and Spell Effectiveness: Step C.2), none at run time.
// Menu-neutral: a Target names what one menu's widgets are called; the colours and art come from the preset
// (uistyle::SkinFor). Inventory uses it now; the container and barter menus add their own Target.
#include "core/UiTheme.h"
#include "engine/Ui.h"
namespace menuskin
{
	struct Target
	{
		const char*        name;            // for logs
		const char*        detailsClass;    // the item details widget class
		const char*        rowName;         // the row's name column
		const char* const* rowValues;       // the row's stat columns (each holds an "inv_entry_value" label)
		int                rowValueCount;
		const char* const* footers;         // footer fields (armour, gold, weight)
		int                footerCount;
		bool               stolenFilter;    // vendors have no stolen items: their barter tabs have no stolen filter
	};

	inline constexpr const char* kInventoryRowValues[]{ "inv_entry_health", "inv_entry_weapon_damage",
		"inv_entry_armor_rating", "inv_entry_weight", "inv_entry_value" };
	inline constexpr const char* kInventoryFooters[]{ "inv_armor", "inv_money", "inv_weight" };
	inline constexpr Target kInventory{ "Inventory", "WBP_OriginalMenu_InventoryItemDetails_C", "inv_entry_name",
		kInventoryRowValues, 5, kInventoryFooters, 3, true };
	// Magic: rows are WBP_ModernMenu_MagicEntry (colours baked, Step C.2), no footers.
	inline constexpr Target kMagic{ "Magic", "WBP_ModernMenu_MagicDetails_C", "Magic_entry_name", nullptr, 0, nullptr, 0, false };

	const uistyle::Skin& Current();   // the skin of the configured preset

}
