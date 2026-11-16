#pragma once

#include "engine/Menus.h"

// ============================================================================================================
// Enchanting and Spellmaking altars (WBP_ModernMenu_EnchantmentMenu_C, WBP_ModernMenu_SpellMakingMenu_C): the controller
// keyboard for the item / spell name box (C4-8, owner 2026-11-16: "add the feature of the controller keyboard on this
// screen, the one we added on the alchemy"; "we also need this in the spell making").
// L3 opens our keyboard on EnchantedItem_NameBox / SpellMakingItem_NameBox; each menu's own
// UpdateRenameBoxFocus(New Focus State) starts and stops the native editing, and the name lives only in the box (the menu
// reads it in "On Enchant Requested"). Facts: knowledge base docs/reverse-engineering/dark-mode-ui.md (build 1.512.105.0).
// Game thread only.
// ============================================================================================================

namespace enchanting
{
	bool SpellmakingSearchAvailable();
	UE::UObject* SpellmakingWidget(); // game thread
	void Tick();                                                          // every frame, from core::Tick
	void Controller(std::uint16_t& a_buttons, short& a_lx, short& a_ly); // L3 on an open altar = our keyboard on its name box
	menus::Menu NamingMenu();                                             // kEnchantName / kSpellName while naming, else kNone
}
