#pragma once

// Shared between the category picker's files (Picker, PickerWidgets, PickerPopup, PickerHeader, PickerTick,
// PickerInput, NativeBackground, ItemRows, CategoryEvaluator); not for other features.

#include "core/DarkStyle.h"
#include "sorting/Picker.h"
#include "sorting/rules/ControllerPolicy.h"
#include "sorting/rules/ItemSort.h"
#include "sorting/rules/InventoryHeaderLayout.h"
#include "sorting/rules/ItemTags.h"
#include "sorting/rules/MagicRows.h"
#include "sorting/rules/PickerHighlight.h"
#include "sorting/rules/PickerNavigation.h"
#include "sorting/rules/StolenFilter.h"
#include "sorting/rules/EnchantedFilter.h"
#include "sorting/rules/TransferHold.h"
#include "engine/Reflect.h"

#include <unordered_map>

namespace sorting
{
	namespace detail
	{
		constexpr float kClear[4] = { 0, 0, 0, 0 };
		// Focus colour/flat geometry are defined in PickerHighlight.h; applied-filter state is not a highlight.
		// Item masks: low 32 bits = the first picker's tags, high 32 bits = the second picker's (Magic effect). A row that is
		// not a spell (for example an active effect) carries kPassAll and is never hidden.
		constexpr std::uint64_t kPassAll = magicrows::kPassAll;
		struct Rect
		{
			double x = 0, y = 0, w = 0, h = 0;
			bool Hit(double px, double py) const { return w > 0 && h > 0 && px >= x && py >= y && px < x + w && py < y + h; }
		};

		// Inventory/Magic share slot 0; player and merchant/container each own an
		// independent snapshot and TES request. Clearing one side cannot clear the other.
		struct FilterState
		{
			bool dirty = true, dataReady = false;
			int menu = 0;
			std::vector<int> indices;
			std::unordered_map<int, std::uint64_t> keys;  // classification index -> stable item key
			std::unordered_map<int, std::uint64_t> masks;
			std::vector<UE::UObject*> filtered;
			std::vector<enchantedfilter::Status> enchanted;
			std::vector<stolenfilter::Status> stolen;  // by source position, read only while the toggle is on
			std::uint64_t generation = 0, ticket = 0, dataDeadline = 0;
		};
		extern FilterState g_states[3];
		void ResetShared(unsigned slot);

		// ---- widget helpers (PickerWidgets.cpp) ----
		void Brush(UE::UObject* a_image, const wchar_t* a_icon, bool a_inventoryDark = false);
		bool TextureBrush(UE::UObject* a_image, const wchar_t* a_gamePath);   // a game texture, loaded at run time
		// one of our PNGs (MenusRevamped/Sorting/<file>), drawn in a_tint (a white shape, DarkStyle.h); false if missing
		bool PngBrush(UE::UObject* a_image, const wchar_t* a_file, const darkstyle::Rgb* a_tint = nullptr);
		UE::UObject* CanvasChild(UE::UObject* a_canvas, const wchar_t* a_class, const wchar_t* a_name, double x, double y, double w, double h, int z = 0, UE::UObject** a_slot = nullptr);
		void LayoutChild(UE::UObject* widget, double x, double y, double width, double height);
		bool PickerFont(UE::UObject* label, double size);
		void BoundLabel(UE::UObject* label);

		// ---- reading item and spell rows (ItemRows.cpp) ----
		int ContainerPage(int a_kind);
		int Index(UE::UObject* a_item);
		std::uint32_t ContainerForm(UE::UObject* item, std::uint64_t* a_key = nullptr);
		stolenfilter::Status StolenStatus(UE::UObject* item);
		enchantedfilter::Status EnchantedStatus(UE::UObject* item);
		void ComputeMagicMasks(const std::vector<UE::UObject*>& a_items, FilterState& state);
		// The item key of an OriginalInventoryMenuItemProperties struct (form id, else name + weight + type), as the
		// classification cache uses it; 0 when unreadable.
		std::uint64_t RowKey(const std::uint8_t* a_properties);
		bool CachedRowTags(std::uint64_t a_key, std::uint64_t& a_tags);   // CategoryEvaluator.cpp
		struct MagicScroll { int index = -1; magicrows::Spell magic; };
		struct MagicLists { std::vector<magicrows::Spell> spells; std::vector<MagicScroll> scrolls; };
		MagicLists ReadMagicLists();
		std::uint64_t MagicRowMask(unsigned a_range, unsigned a_school, int a_index, const MagicLists& a_lists);

		// ---- our header widgets (PickerHeader.cpp) ----
		extern std::vector<reflect::Handle> g_ownedHeaders;
		extern std::uint64_t g_nativePeerRevision;
		bool IsOwnedHeader(UE::UObject* widget);
		bool OwnHeaderEvent(UE::UObject*, UE::UFunction*, void*);

		// One header icon plus its popup. Magic has school, source type and effect pickers.
		struct Picker
		{
			int kind = -1;  // -1 inactive, 0 inventory, 1/2 Magic school/effect, 3/4 container sides, 5 Magic type, 6 Magic Active Effects sorts
			std::mutex inputLock;
			Rect headerPx, popupPx, cellsPx[itemtags::kMaxChoices];
			bool inputActive = false, inputOpen = false, swallowUp = false, swallowEscape = false, swallowRightUp = false;
			bool groupInput = false;   // under inputLock: a left click on the filter sorts by category (its sort host is live)
			std::uint64_t doubleClickUntil = 0;
			int command = -1;
			bool commandFromController = false;
			std::atomic<bool> active{ false };
			bool open = false, paintedOpen = false, controllerFocus = false, controllerPopup = false;
			std::atomic<int> mouseHoverCell{ -1 };
			int paintedMouseHoverCell = -1;
			std::atomic<bool> mouseInputActive{ false };
			bool paintedMouseInputActive = false;
			bool mousePositionKnown = false; // under inputLock; re-hit-test after popup layout changes
			double mouseX = 0, mouseY = 0;
			unsigned cursor = 0;
			pickerpad::ReturnToList returnToList;
			reflect::Handle carousel;
			std::vector<reflect::Handle> nativePeers;
			bool nativeFocusWarning = false;
			unsigned choice = 0, appliedChoice = 0;  // flat membership ID, independent of popup depth
			unsigned category = 0, appliedCategory = 0;  // All Items page only: 0 = none, else 1 weapons, 2 gear, 3 potions, 4 misc
			itemtags::Tab tab = itemtags::Tab::Gear, view = itemtags::Tab::Gear;  // view: whose choices the popup shows
			bool tabSet = false, styled = false, widgetWarning = false, fontsReady = false, fontWarning = false;
			reflect::Handle menu, canvas, header, headerImage, headerFrame, popup, popupSlot, paperSlot, nativeSchool;
			reflect::Handle cells[itemtags::kMaxChoices], icons[itemtags::kMaxChoices], labels[itemtags::kMaxChoices], title;
			std::uint64_t nextLayout = 0, nextHitMeasure = 0, nextFontTry = 0;
			transferhold::Hold transfer;  // RMI-004: container side only
			// Stolen-only icon (Inventory and player side): kept across native tabs, off when the menu closes.
			bool stolenOnly = false, appliedStolen = false, stolenFocus = false;
			reflect::Handle stolenHeader, stolenImage, stolenFrame;
			Rect stolenPx;
			bool enchantedOnly = false, appliedEnchanted = false, enchantedFocus = false;
			reflect::Handle enchantedHeader, enchantedImage, enchantedFrame;
			Rect enchantedPx;
			// Own sort buttons (owner 2026-11-07): Inventory and both container sides; every native sort wrapper collapsed.
			static constexpr int kSorts = static_cast<int>(itemsort::Key::kCount);
			reflect::Handle sortHeaders[kSorts], sortImages[kSorts], sortFrames[kSorts], sortArrows[kSorts], sortBacks[kSorts];
			Rect sortPx[kSorts];
			int sortFocus = -1;   // our sort button with controller focus, -1 none
			std::vector<reflect::Handle> nativeSorts;
			// Per Inventory/container tab (Weapons..AllItems) or Magic spell page (Target..All); kept across tabs and openings.
			// Slots 0-4 Inventory/container tabs, 5 Magic Active Effects (kind 6), 6-9 Magic spell pages (own memory: the
			// Inventory and Magic share g_pickers[0]).
			static constexpr int kMagicSorts = 6;
			static constexpr int kSpellPurchaseSort = 10;
			itemsort::State sorts[kSpellPurchaseSort + 1];
			unsigned seededKinds = 0;   // kinds whose slots got the INI default (bCategorySort) once
			bool pendingRepublish = false;   // the classification landed for rows the category sort could not place
			reflect::Handle groupBack, groupArrow;   // the category sort shown on the filter like an active sort
			reflect::Handle inventoryListSlot;
			struct InventoryClip { reflect::Handle widget; std::uint8_t clipping; };
			std::vector<InventoryClip> inventoryClips;
			std::array<std::array<double, 2>, kSorts + 3> inventoryTranslations{};
			std::array<reflect::Handle, inventoryheaders::kGuides> inventoryHeaderGuides{};
			inventoryheaders::Box inventoryMeasuredList, inventoryMeasuredStatus;
			std::array<inventoryheaders::Box, kSorts> inventoryMeasuredColumns{};
			double inventoryMeasuredNameX=0;
			bool inventoryHeaderMeasured=false;
			inventoryheaders::TabMeasures<10> inventoryTabMeasures;   // per itemtags::Tab (RMI-077)
			double inventoryRowsBottom=0;
			std::array<double, inventoryheaders::kPanelChildren> inventorySlotWidths{-1,-1,-1,-1,-1,-1,-1,-1,-1};   // RMI-077
			void CompensateInventorySlots();
			reflect::Handle inventoryHint;                       // the native controller Y glyph (RMI-078)
			std::array<double, 2> inventoryHintTranslation{};
			std::vector<reflect::Handle> inventoryStatusImages;
			std::array<std::array<double, 2>, 4> inventoryStatusOffsets{{
				{0,0}, {-9,0}, {0,-45}, {-9,-29}
			}};
			std::array<std::array<bool, 2>, 4> inventoryStatusMeasured{};   // RMI-093: which masks have a measured offset
			bool inventoryStatusReady=false;
			std::array<float, 4> inventoryListBase{};
			bool inventoryListPadding = false, inventoryListReserved = false;
			double inventoryHeaderY = 0;
			double inventoryHeaderFrame = -1;
			bool inventoryHeaderReady = false;
			bool inventoryFillPending = true;
			void SetInventoryFillOpacity(float opacity);
			void SetInventoryHeaderOpacity(float opacity);
			bool LayoutInventoryHeaders(UE::UObject* a_menu, UE::UObject* a_canvas, bool measureColumns=true);
			void HideInventoryHeaderLayout();
			void ClearInventoryHeaderLayout();
			int magicPage = 0, paintedSlot = -1;
			// Quick loot has no native sort row: a Y the game ignored opens/closes ours (deadline, wanted focus).
			std::uint64_t quickLootY = 0;
			bool quickLootWant = false;
			reflect::Handle quickLootHint;   // the side filter's own Y glyph (a WBP_ModernPrefab_ControllerInputHint)
			int quickLootHintShown = -1;     // last visibility we set: -1 none, 0 hidden, 1 shown
			std::vector<reflect::Handle> quickLootShown;   // the glyph and the parents we made visible (hidden again on mouse)
			// kind 6: Magic Active Effects sort host (a free container slot, page 4 only, no category picker)
			bool SpellPurchase() const { return kind == 7; }
			bool OwnSorts() const { return kind == 0 || kind == 1 || kind == 3 || kind == 4 || kind == 6 || SpellPurchase(); }
		// The single header row (strip under the title, sorts over their columns): Inventory, and since the Magic revamp the
		// Magic spell pages (the School host, which also places the Effect and Type pickers) and Active Effects. The
		// inventory* layout fields below are this row's state for all three.
		// The Inventory strip (Inventory rows): the Inventory and both container/barter sides (owner, C.1 2026-11-12: the same
		// WBP_ModernMenu_ContainerInventory_MainContent lays out each side).
		bool InventoryStrip() const { return kind == 0 || kind == 3 || kind == 4; }
		bool HeaderLayout() const { return InventoryStrip() || kind == 1 || kind == 6 || SpellPurchase(); }
			std::vector<itemsort::Key> SortKeys() const { return SpellPurchase() ? itemsort::SpellPurchaseKeys() : kind == 1 ? itemsort::MagicKeys() : kind == 6 ? itemsort::ActiveKeys() : itemsort::Keys(tab); }
			bool Offers(itemsort::Key a_key) const { const auto keys = SortKeys(); return std::find(keys.begin(), keys.end(), a_key) != keys.end(); }
			int SortSlot() const { return SpellPurchase() ? kSpellPurchaseSort : kind == 1 ? kMagicSorts + magicPage : kind == 6 ? 5 : static_cast<int>(tab); }
			itemsort::State& SortAt(int a_slot) { return sorts[a_slot >= 0 && a_slot <= kSpellPurchaseSort ? a_slot : 4]; }
			// Category sort: the picker whose sort memory this filter uses (Magic school/effect/type: the school picker).
			Picker* SortHost();
			bool GroupSorts() { auto* host = SortHost(); return kind != 6 && host && host->OwnSorts(); }
			void SeedSorts();   // the INI default for this kind's slots, once
			void PaintGroup();  // parchment + arrow on the filter while its category sort is active
			bool OwnsSortHeader(UE::UObject* a_widget) const { for (const auto& h : sortHeaders) if (a_widget && reflect::Get(h) == a_widget) return true; return false; }

			unsigned Shift() const { return kind == 2 ? 32u : 0u; }

			// PickerTick.cpp: membership, reset and the per-frame pass
			bool Passes(std::uint64_t a_mask) const;
			void Reset();
			void Deactivate();
			bool HideNativeReplacement();   // true when a collapse was attempted (the game had re-shown it)
			void Tick(int a_kind, UE::UObject* a_menu, UE::UObject* a_canvas, float a_viewportScale);
			void ShowTab(itemtags::Tab a_tab);   // another tab: reset its choices, show and paint its own sorts

			// PickerPopup.cpp: the popup grid and painting
			bool BuildPopup(UE::UObject* a_canvas);
			void OpenPopup();  // closes peer popups without resetting their applied filters
			const wchar_t* HeaderIcon() const;
			void BrushHeader(const wchar_t* a_icon);   // the header icon; Magic school/type "All" = the game's own texture
			unsigned Highlight() const;
			void StyleCells();
			void PaintStolen();
			void PaintEnchanted();
			void PaintController();
			void PaintSorts();
			bool HideNativeSorts();   // true when a collapse was attempted (the game had re-shown one)
			void ShowOwnWidgets();
			void ShowQuickLootHint();   // quick loot: the game hides the filter's Y glyph; on a controller we show it   // every custom widget of the bar in the same pass (owner 2026-11-07)

			// PickerHeader.cpp: our icons in the native sort row (and RMI-004 Transfer detach)
			bool InsertHeader(UE::UObject* a_panel, UE::UObject* navigation);
			void DetachForTransfer();
			void ClearOwnFromBar();   // all our widgets out of the native bar (they come back together, owner 2026-11-07)
			void ClearHitAreas();     // no click lands on where our widgets were
			static bool NativeNameShown(UE::UObject* a_panel);

			// PickerInput.cpp: controller focus, mouse and keys
			bool SortCarouselFocused();
			bool Focused(const reflect::Handle& a_widget);
			bool ControllerFocused() { return Focused(header); }
			bool StolenFocused() { return Focused(stolenHeader); }
			bool EnchantedFocused() { return Focused(enchantedHeader); }
			int SortFocused();
			void Queue(int cmd);
			bool Message(UINT a_message, WPARAM a_key, LPARAM a_position);

			// NativeBackground.cpp: the native tiles' background mask
			UE::UObject* NativeInner(UE::UObject* wrapper);
			void WarnNativeFocus();
		};

		extern Picker g_pickers[5];  // Inventory/Magic school, Magic effect, player side, other side, Magic type

		void UpdateNativeBackgroundMask();   // NativeBackground.cpp

		// OwnSort.cpp: the list order and the republish after a click
		void WatchRefills(Picker& a_picker);
		void Republish(Picker& a_picker);
		void TagsLanded(unsigned a_slot);   // Evaluate: classification arrived; republish once if grouped rows waited for it
		const wchar_t* AllTexture(int a_kind);   // the game's own icon for a Magic school/type "All" (header and popup)
		bool IsOwnSortHeader(UE::UObject* a_widget);
	}
}
