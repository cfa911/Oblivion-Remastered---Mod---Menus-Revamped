// Compact rows (owner 2026-11-10/11): the FuzzUI - Interface Tweaks layout numbers set at run time, so the owner no longer needs
// its 27 replacement blueprints. The blueprints are cooked without property names, so each change is applied only where the
// widget's own field still holds the documented stock value (another mod's layout, or an already compact widget, is left alone).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#if __has_include("../src/layout/rules/CompactRowsTable.h")
#include "../src/layout/rules/CompactRowsTable.h"
#else
// Pre-fix behaviour: no compact rows exist.
namespace compactrows {
enum class Kind { Box, Slot, Font };
enum class Group { Magic, Inventory, Crafting, StatsQuest };
enum class Fit { Applied, AlreadyCompact, Ambiguous, Missing };
struct Change { const char* widget; Kind kind; float stock; float compact; int count; };
struct Blueprint { const wchar_t* classPath; const char* name; Group group; const Change* changes; int changeCount; };
inline constexpr Blueprint kBlueprints[1]{ { L"", "", Group::Magic, nullptr, 0 } };
inline constexpr int kBlueprintCount = 0;
inline Fit Retarget(float*, int, const Change&) { return Fit::Missing; }
}
#endif
using namespace compactrows;
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", why); } }

const Blueprint* Find(const char* name)
{
	for (int i = 0; i < kBlueprintCount; ++i) if (std::strcmp(kBlueprints[i].name, name) == 0) return &kBlueprints[i];
	return nullptr;
}
const Change* FindChange(const char* bp, const char* widget, float stock)
{
	const auto* b = Find(bp);
	for (int i = 0; b && i < b->changeCount; ++i)
		if (std::strcmp(b->changes[i].widget, widget) == 0 && std::fabs(b->changes[i].stock - stock) < 0.01f) return &b->changes[i];
	return nullptr;
}
const Change* FindChange(const char* bp, const char* widget, float stock, float compact)
{
	const auto* b = Find(bp);
	for (int i = 0; b && i < b->changeCount; ++i) {
		const auto& c = b->changes[i];
		if (std::strcmp(c.widget, widget) == 0 && std::fabs(c.stock - stock) < 0.01f && std::fabs(c.compact - compact) < 0.01f) return &c;
	}
	return nullptr;
}
bool Is(const char* bp, const char* widget, Kind kind, float stock, float compact, int count = 1)
{
	const auto* c = FindChange(bp, widget, stock, compact);
	return c && c->kind == kind && c->count == count;
}

int main()
{
	// --- the matching rule ---
	{
		const Change row{ "SizeBox_0", Kind::Box, 54.0f, 36.0f, 1 };
		float box[2]{ 0.0f, 54.0f };   // WidthOverride unset, HeightOverride stock
		Check(Retarget(box, 2, row) == Fit::Applied && box[1] == 36.0f && box[0] == 0.0f, "a stock row height becomes 36, the other field stays");
		Check(Retarget(box, 2, row) == Fit::AlreadyCompact && box[1] == 36.0f, "a second pass sees the row already compact and writes nothing");
		float other[2]{ 0.0f, 40.0f };   // another mod's layout
		Check(Retarget(other, 2, row) == Fit::Missing && other[1] == 40.0f, "a non-stock value (another mod) is left alone");
		const Change icon{ "SizeBox_1", Kind::Box, 48.0f, 32.0f, 2 };
		float both[2]{ 48.0f, 48.0f };
		Check(Retarget(both, 2, icon) == Fit::Applied && both[0] == 32.0f && both[1] == 32.0f, "an icon box with two stock fields gets both");
		const Change pad{ "OverlaySlot_0", Kind::Slot, 10.0f, 50.0f, 1 };
		float margin[4]{ 10.0f, 10.0f, 10.0f, 10.0f };
		Check(Retarget(margin, 4, pad) == Fit::Ambiguous && margin[0] == 10.0f && margin[3] == 10.0f,
			"more stock matches than documented changes: ambiguous, nothing written");
		float near[2]{ 0.0f, 485.4470f };
		const Change list{ "SizeBox_117", Kind::Box, 485.447f, 550.0f, 1 };
		Check(Retarget(near, 2, list) == Fit::Applied && near[1] == 550.0f, "printed stock values (6 digits) match within 0.01");
		const Change two{ "SizeBox_1", Kind::Box, 54.0f, 48.0f, 1 }, three{ "SizeBox_1", Kind::Box, 64.0f, 48.0f, 1 };
		float mixed[2]{ 54.0f, 64.0f };
		Check(Retarget(mixed, 2, two) == Fit::Applied && Retarget(mixed, 2, three) == Fit::Applied && mixed[0] == 48.0f && mixed[1] == 48.0f,
			"two changes on one widget (54 and 64 to 48) both land");
		Check(Retarget(mixed, 2, two) == Fit::AlreadyCompact && Retarget(mixed, 2, three) == Fit::AlreadyCompact, "and are idempotent");
	}

	// --- the table: every menu the owner chose (2026-11-11: all) ---
	std::set<int> groups;
	bool paths = kBlueprintCount > 0;
	for (int i = 0; i < kBlueprintCount; ++i) {
		const auto& b = kBlueprints[i];
		groups.insert(int(b.group));
		const std::wstring p = b.classPath;
		const std::wstring tail = L"." + std::wstring(b.name, b.name + std::strlen(b.name)) + L"_C";
		paths = paths && p.starts_with(L"/Game/UI/") && p.ends_with(tail) && b.changeCount > 0;
	}
	Check(groups.size() == 4, "Magic, Inventory, crafting and Stats/Quest groups all present");
	Check(paths, "every class path is /Game/UI/.../<name>.<name>_C with at least one change");

	// row heights 54 -> 36 on every list row FuzzUI shrinks
	for (const char* row : { "WBP_ModernMenu_MagicEntry", "WBP_ModernMenu_EnchantmentEntry",
		"WBP_ModernMenu_SpellmakingEntry", "WBP_ModernMenu_SpellPurchaseItem", "WBP_Modern_QuestEntry" })
		Check(Is(row, "SizeBox_0", Kind::Box, 54, 36), (std::string(row) + " row SizeBox_0 54 -> 36").c_str());
	Check(Is("WBP_ModernMenu_StatsSkillEntry", "CanvasPanelSlot_5", Kind::Slot, 54, 36), "Stats skill row slot 54 -> 36");
	// icons 48 -> 32 (two fields each)
	Check(Is("WBP_ModernMenu_MagicEntry", "SizeBox_1", Kind::Box, 48, 32, 2), "Magic row icon 48 -> 32");
	Check(Is("WBP_ModernMenu_SpellPurchaseItem", "effect_icon_sizebox", Kind::Box, 48, 32, 2), "Spell Purchase row icon 48 -> 32");
	Check(Is("WBP_Modern_QuestEntry", "CanvasPanelSlot_4", Kind::Slot, 48, 32, 2), "Quest row icon slot 48 -> 32");
	Check(Is("WBP_OriginalMenu_InventoryIconEntry", "SizeBox_60", Kind::Box, 48, 32, 2), "Inventory row icon 48 -> 32");
	Check(Is("WBP_OriginalMenu_InventoryIconEntry", "inv_entry_icon_count", Kind::Font, 12, 11), "Inventory icon count font 12 -> 11");
	// FuzzUI's InventoryEntry / MainPart do load (same package ids as stock; owner 2026-11-11: its Inventory rows are shorter)
	Check(Is("WBP_OriginalMenu_InventoryEntry", "SizeBox_0", Kind::Box, 54, 36), "Inventory row SizeBox_0 54 -> 36");
	Check(Is("WBP_OriginalMenu_InventoryMainPart", "CanvasPanelSlot_2", Kind::Slot, 138, 131), "Inventory main part slot 138 -> 131");
	Check(Is("WBP_OriginalMenu_InventoryMainPart", "CanvasPanelSlot_2", Kind::Slot, 52, 22), "Inventory main part slot 52 -> 22");
	for (const char* footer : { "inv_armor", "inv_money", "inv_weight" })
		Check(Is("WBP_OriginalMenu_InventoryMainPart", footer, Kind::Font, 16.5f, 14), (std::string(footer) + " footer font 16.5 -> 14").c_str());
	// Plus Alchemy: the list view's stretched slot gets margins -5 / -81 where stock has 0 on all four sides (a value match would be
	// ambiguous): such changes name the field (0 Left, 1 Top, 2 Right, 3 Bottom)
	{
		const Change bottom{ "CanvasPanelSlot_0", Kind::Slot, 0.0f, -81.0f, 1, 3 };
		float m[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
		Check(Retarget(m, 4, bottom) == Fit::Applied && m[3] == -81.0f && m[0] == 0.0f && m[2] == 0.0f, "a named field changes alone");
		Check(Retarget(m, 4, bottom) == Fit::AlreadyCompact, "and reads already compact afterwards");
		float other[4]{ 0.0f, 0.0f, 0.0f, 12.0f };
		Check(Retarget(other, 4, bottom) == Fit::Missing && other[3] == 12.0f, "a named field holding another value is left alone");
	}
	Check(Is("WBP_ModernMenu_AlchemyItemsListView", "CanvasPanelSlot_0", Kind::Slot, 0, -81) &&
		FindChange("WBP_ModernMenu_AlchemyItemsListView", "CanvasPanelSlot_0", 0, -81)->field == 3, "Plus Alchemy list extends 81 down (Bottom)");
	// Plus Alchemy, repeated slot names (every same-named export compared, 2026-11-11): the effects block's padding 570 -> 665, and the
	// filter list's slot (padding 10,0,10,0 among other OverlaySlot_0 with 0,0,0,0) gets bottom -75: a guard field picks the right one
	Check(Is("WBP_ModernMenu_AlchemyMenu", "OverlaySlot_2", Kind::Slot, 570, 665), "Plus Alchemy effects block 570 -> 665");
	{
		const auto* c = FindChange("WBP_ModernMenu_AlchemyFilters", "OverlaySlot_0", 0, -75);
		Check(c && c->field == 3 && c->guardField == 0 && std::fabs(c->guardValue - 10.0f) < 0.01f, "Plus Alchemy filter list bottom -75, guarded by left 10");
		if (c) {
			float header[4]{ 0, 0, 0, 0 }, list[4]{ 10, 0, 10, 0 };
			Check(Retarget(header, 4, *c) == Fit::Missing && header[3] == 0.0f, "the header row's OverlaySlot_0 (left 0) is left alone");
			Check(Retarget(list, 4, *c) == Fit::Applied && list[3] == -75.0f, "the filter list's slot gets bottom -75");
		}
	}
	// owner 2026-11-11 "Magic like the Inventory" (layout dump): VerticalBox_0 (bars + effectiveness line) sits in slot
	// VerticalBoxSlot_3 with padding (0,20,0,30); tightened to (0,4,0,10) so the spell list gets the room (our values, not FuzzUI's)
	Check(Is("WBP_ModernMenu_MagicMenu", "VerticalBoxSlot_3", Kind::Slot, 20, 4), "Magic gap above the bars 20 -> 4");
	Check(Is("WBP_ModernMenu_MagicMenu", "VerticalBoxSlot_3", Kind::Slot, 30, 10), "Magic gap under the effectiveness line 30 -> 10");
	Check(Is("WBP_ModernMenu_MagicMenu", "VerticalBoxSlot_0", Kind::Slot, 20, 0), "FuzzUI's bars-to-effectiveness gap 20 -> 0 stays");
	// owner round 4 (2026-11-11): Stats skill rows stayed ~64 tall. SizeBox_1 has 54 (override) and 64 (another size pair); after
	// 54 -> 48 the overrides read "already compact" and the 64 pair was never looked at.
	{
		float pairs[3][2]{ { 688.0f, 54.0f }, { 0.0f, 64.0f }, { 0.0f, 0.0f } };
		const Change first{ "SizeBox_1", Kind::Box, 54.0f, 48.0f, 1 }, second{ "SizeBox_1", Kind::Box, 64.0f, 48.0f, 1 };
		Check(RetargetPairs(pairs, 3, first) == Fit::Applied && pairs[0][1] == 48.0f, "Stats SizeBox_1 override 54 -> 48");
		Check(RetargetPairs(pairs, 3, second) == Fit::Applied && pairs[1][1] == 48.0f, "Stats SizeBox_1 64 in the next pair -> 48");
		Check(RetargetPairs(pairs, 3, second) == Fit::AlreadyCompact && RetargetPairs(pairs, 3, first) == Fit::AlreadyCompact,
			"both read already compact afterwards");
		float none[3][2]{ { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f } };
		Check(RetargetPairs(none, 3, second) == Fit::Missing, "no stock and no compact value anywhere: missing");
		float icon[3][2]{ { 0.0f, 0.0f }, { 48.0f, 48.0f }, { 0.0f, 0.0f } };
		const Change iconChange{ "SizeBox_60", Kind::Box, 48.0f, 32.0f, 2 };
		Check(RetargetPairs(icon, 3, iconChange) == Fit::Applied && icon[1][0] == 32.0f && icon[1][1] == 32.0f, "Inventory icon pair still found");
	}
	// owner round 3 (2026-11-11): the gap above the bars and footer; FuzzUI zeroes two spacers (Size doubles: Y 21 and 33)
	Check(Is("WBP_OriginalMenu_InventoryMainPart", "Spacer_2", Kind::Spacer, 21, 0), "Inventory Spacer_2 height 21 -> 0");
	Check(Is("WBP_OriginalMenu_InventoryMainPart", "Spacer_3", Kind::Spacer, 33, 0), "Inventory Spacer_3 height 33 -> 0");
	{
		const Change gap{ "Spacer_2", Kind::Spacer, 21.0f, 0.0f, 1 };
		float size[2]{ 1.0f, 21.0f };
		Check(Retarget(size, 2, gap) == Fit::Applied && size[0] == 1.0f && size[1] == 0.0f, "a spacer's height goes to 0, its width stays");
		Check(Retarget(size, 2, gap) == Fit::AlreadyCompact, "a zeroed spacer reads already compact");
	}
	if (const auto* b = Find("WBP_OriginalMenu_InventoryEntry"))
		Check(std::wstring(b->classPath).find(L"/Inventory/MainPart/WBP_OriginalMenu_InventoryEntry.") != std::wstring::npos,
			"Inventory entry class path is the stock one (Inventory/MainPart)");
	// Stats fonts and icons
	Check(Is("WBP_ModernMenu_StatsSkillEntry", "stat_skillentry_icon_sizebox", Kind::Box, 64, 48, 2), "Stats skill icon 64 -> 48");
	Check(Is("WBP_ModernMenu_StatsSkillEntry", "stat_skillentry_label", Kind::Font, 25.5f, 22.5f), "Stats skill label font");
	Check(Is("WBP_ModernMenu_StatsSkillEntry", "stat_skillentry_value", Kind::Font, 18, 16), "Stats skill value font");
	Check(Is("WBP_ModernMenu_Stats_LevelDescription", "stat_level_name", Kind::Font, 25.5f, 23.5f), "Stats level name font");
	// taller list panels
	Check(Is("WBP_ModernMenu_EnchantmentMenu", "ListView_SizeBox", Kind::Box, 540, 590), "Enchanting list 540 -> 590");
	Check(Is("WBP_ModernMenu_SpellMakingMenu", "CanvasPanelSlot_1", Kind::Slot, 709.684f, 735), "Spellmaking list 709.7 -> 735");
	Check(Is("WBP_ModernMenu_SpellPurchase", "CanvasPanelSlot_5", Kind::Slot, 320, 355), "Spell Purchase list 320 -> 355");
	// owner Alchemy check 2026-11-11: FuzzUI's 550 ran the list into the ingredient effects (Menus Revamped adds a search bar
	// and sort row in that column); ours is 495, 10 over stock
	Check(!FindChange("WBP_ModernMenu_AlchemyMenu", "SizeBox_117", 485.447f), "Alchemy list SizeBox_117 stays stock (Plus Alchemy)");
	Check(Is("WBP_ModernMenu_MagicMenu", "Magic_SpellEffectivenes_Value", Kind::Font, 21, 18), "Magic effectiveness font 21 -> 18");
	// Alchemy filter panel
	// owner 2026-11-11: Alchemy follows FuzzUI Plus Alchemy (it wins over Interface Tweaks in the owner's reference screenshot)
	Check(Is("WBP_ModernMenu_AlchemyItemEntry", "SizeBox_0", Kind::Box, 54, 27), "Plus Alchemy row 54 -> 27");
	Check(Is("WBP_ModernMenu_AlchemyItemEntry", "Icon_SizeBox", Kind::Box, 48, 26, 2), "Plus Alchemy icon box 48 -> 26");
	Check(Is("WBP_ModernMenu_AlchemyItemEntry", "CanvasPanelSlot_4", Kind::Slot, 2, 26), "Plus Alchemy icon slot left 2 -> 26");
	Check(Is("WBP_ModernMenu_AlchemyItemEntry", "CanvasPanelSlot_4", Kind::Slot, 48, 26, 2), "Plus Alchemy icon slot size 48 -> 26");
	Check(Is("WBP_ModernMenu_AlchemyItemEntry", "CanvasPanelSlot_1", Kind::Slot, 20, 18), "Plus Alchemy name slot 20 -> 18");
	Check(!FindChange("WBP_ModernMenu_AlchemyItemEntry", "CanvasPanelSlot_1", 84), "name x stays 84 (Interface Tweaks' 65 dropped)");
	// owner 2026-11-11: the ingredient page parchment (Alchemy_IngredientsListBG) itself grows up to the screen top (where Menus
	// Revamped's Alchemy-TopParchment strip was, 93.7 above it) and down to the owner's mark (bottom 980.2): no strip image
	Check(Is("WBP_ModernMenu_AlchemyMenu", "CanvasPanelSlot_0", Kind::Slot, 60.7868f, -32.9132f), "Alchemy parchment top 60.8 -> -32.9");
	Check(Is("WBP_ModernMenu_AlchemyMenu", "CanvasPanelSlot_0", Kind::Slot, 809.418f, 1013.118f), "Alchemy parchment height 809.4 -> 1013.1");
	Check(Is("WBP_ModernMenu_AlchemyMenu", "CanvasPanelSlot_1", Kind::Slot, 712.043f, 812.043f), "Plus Alchemy panel 712.0 -> 812.0");
	Check(Is("WBP_ModernMenu_AlchemyFilters", "CanvasPanelSlot_0", Kind::Slot, 744.831f, 755.831f), "Plus Alchemy filters 744.8 -> 755.8");
	Check(Is("WBP_ModernMenu_AlchemyFilters", "CanvasPanelSlot_2", Kind::Slot, 64, 30), "Plus Alchemy filters slot 64 -> 30");
	Check(!FindChange("WBP_ModernMenu_AlchemyFilters", "SizeBox_1", 470.672f), "Interface Tweaks' filter panel 570 dropped");
	Check(Is("WBP_ModernMenu_FilterEffectEntry", "SizeBox", Kind::Box, 58, 34), "Plus Alchemy filter entry 58 -> 34");
	Check(Is("WBP_ModernMenu_FilterEffectEntry", "EffectText", Kind::Font, 20, 16), "Plus Alchemy filter text 20 -> 16");
	for (const char* pad : { "OverlaySlot_0", "OverlaySlot_4", "OverlaySlot_5" })
		Check(Is("WBP_ModernMenu_FilterEffectEntry", pad, Kind::Slot, 5, 2, 4), (std::string(pad) + " padding 5 -> 2 all sides").c_str());
	Check(!Find("WBP_ModernMenu_Alchemy_FilterEffectButton"), "filter button stays stock (Plus Alchemy)");
	Check(Is("WBP_ModernMenu_ItemEffect", "EffectText", Kind::Font, 18, 22), "Plus Alchemy ingredient effect text 18 -> 22");

	// slots are named *Slot*, fonts are never on size boxes
	bool kinds = kBlueprintCount > 0;
	for (int i = 0; i < kBlueprintCount; ++i)
		for (int j = 0; j < kBlueprints[i].changeCount; ++j) {
			const auto& c = kBlueprints[i].changes[j];
			const bool slotName = std::strstr(c.widget, "Slot") != nullptr;
			kinds = kinds && (slotName == (c.kind == Kind::Slot)) && c.count >= 1 && c.count <= 4 && c.stock != c.compact;
		}
	Check(kinds, "every change: Slot kind exactly for *Slot* names, 1-4 fields, stock differs from compact");

	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
