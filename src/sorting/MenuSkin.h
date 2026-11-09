#pragma once
// The native menu skin: passes that restyle a list menu's own widgets (rows, footer, title, scrollbar, item details).
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

	const uistyle::Skin& Current();   // the skin of the configured preset

	// The tab title: kept whole in every preset (layout), inked by the skin.
	void StyleTitle(UE::UObject* title);
	// Bumps whenever the native title's text changes (our own title label copies it; RMI-073).
	unsigned TitleTextGeneration();
	// A label of ours takes the preset's ink when the preset recolours (else it keeps what it was given).
	void InkText(UE::UObject* label);
	// Our own title label: the skin keeps it in step with the native title inside the prefab's own SetText.
	void SetTitleLabel(UE::UObject* label);
	// One list row: inks, row separator art, equipped scribble (by the skin). Safe to call on every row rebind.
	void StyleRow(UE::UObject* row, const Target& target);
	// The footer labels under a list: inked now and whenever the game rewrites them.
	void WatchFooters(UE::UObject* mainPart, UE::UObject* menu, const Target& target);
	// The list's scrollbar track and thumbs.
	void StyleListScrollbar(UE::UObject* list);
	// The item details view: restyled when it opens and whenever its item or sub-tab changes.
	void WatchDetails(UE::UObject* menu, UE::UObject* list, const Target& target);
}
