#pragma once

// Shared between the category picker's files (Picker, PickerWidgets, PickerPopup, PickerHeader, PickerTick,
// PickerInput, NativeBackground, ItemRows, CategoryEvaluator); not for other features.

#include "sorting/Picker.h"
#include "sorting/rules/ControllerPolicy.h"
#include "sorting/rules/ItemTags.h"
#include "sorting/rules/MagicRows.h"
#include "sorting/rules/PickerHighlight.h"
#include "sorting/rules/PickerNavigation.h"
#include "sorting/rules/StolenFilter.h"
#include "sorting/rules/TransferHold.h"
#include "engine/Reflect.h"

#include <unordered_map>

namespace sorting
{
	namespace detail
	{
		constexpr float kInk[4] = { 0.20f, 0.14f, 0.09f, 1 };
		constexpr float kTitleInk[4] = { 0.06f, 0.035f, 0.016f, 1 };
		// sRGB #D6D0C8 converted to linear Slate colour.
		constexpr float kPaper[4] = { 0.672443f, 0.630757f, 0.577580f, 1.0f }, kClear[4] = { 0, 0, 0, 0 };
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
			std::vector<stolenfilter::Status> stolen;  // by source position, read only while the toggle is on
			std::uint64_t generation = 0, ticket = 0, dataDeadline = 0;
		};
		extern FilterState g_states[3];
		void ResetShared(unsigned slot);

		// ---- widget helpers (PickerWidgets.cpp) ----
		void Brush(UE::UObject* a_image, const wchar_t* a_icon);
		UE::UObject* CanvasChild(UE::UObject* a_canvas, const wchar_t* a_class, const wchar_t* a_name, double x, double y, double w, double h, int z = 0, UE::UObject** a_slot = nullptr);
		void LayoutChild(UE::UObject* widget, double x, double y, double width, double height);
		bool PickerFont(UE::UObject* label, double size);
		void BoundLabel(UE::UObject* label);

		// ---- reading item and spell rows (ItemRows.cpp) ----
		int ContainerPage(int a_kind);
		int Index(UE::UObject* a_item);
		std::uint32_t ContainerForm(UE::UObject* item, std::uint64_t* a_key = nullptr);
		stolenfilter::Status StolenStatus(UE::UObject* item);
		void ComputeMagicMasks(const std::vector<UE::UObject*>& a_items, FilterState& state);

		// ---- our header widgets (PickerHeader.cpp) ----
		extern std::vector<reflect::Handle> g_ownedHeaders;
		extern std::uint64_t g_nativePeerRevision;
		bool IsOwnedHeader(UE::UObject* widget);
		bool OwnHeaderEvent(UE::UObject*, UE::UFunction*, void*);

		// One header icon plus its popup. Magic has school, source type and effect pickers.
		struct Picker
		{
			int kind = -1;  // -1 inactive, 0 inventory, 1/2 Magic school/effect, 3/4 container sides, 5 Magic type
			std::mutex inputLock;
			Rect headerPx, popupPx, cellsPx[itemtags::kMaxChoices];
			bool inputActive = false, inputOpen = false, swallowUp = false, swallowEscape = false;
			std::uint64_t doubleClickUntil = 0;
			int command = -1;
			bool commandFromController = false;
			std::atomic<bool> active{ false };
			bool open = false, paintedOpen = false, controllerFocus = false, controllerPopup = false;
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

			unsigned Shift() const { return kind == 2 ? 32u : 0u; }

			// PickerTick.cpp: membership, reset and the per-frame pass
			bool Passes(std::uint64_t a_mask) const;
			void Reset();
			void Deactivate();
			void HideNativeReplacement();
			void Tick(int a_kind, UE::UObject* a_menu, UE::UObject* a_canvas, float a_viewportScale);

			// PickerPopup.cpp: the popup grid and painting
			bool BuildPopup(UE::UObject* a_canvas);
			void OpenPopup();  // closes peer popups without resetting their applied filters
			const wchar_t* HeaderIcon() const;
			unsigned Highlight() const;
			void StyleCells();
			void PaintStolen();
			void PaintController();

			// PickerHeader.cpp: our icons in the native sort row (and RMI-004 Transfer detach)
			bool InsertHeader(UE::UObject* a_panel, UE::UObject* navigation);
			void DetachForTransfer();
			static bool NativeNameShown(UE::UObject* a_panel);

			// PickerInput.cpp: controller focus, mouse and keys
			bool SortCarouselFocused();
			bool Focused(const reflect::Handle& a_widget);
			bool ControllerFocused() { return Focused(header); }
			bool StolenFocused() { return Focused(stolenHeader); }
			void Queue(int cmd);
			bool Message(UINT a_message, WPARAM a_key, LPARAM a_position);

			// NativeBackground.cpp: the native tiles' background mask
			UE::UObject* NativeInner(UE::UObject* wrapper);
			void WarnNativeFocus();
		};

		extern Picker g_pickers[5];  // Inventory/Magic school, Magic effect, player side, other side, Magic type

		void UpdateNativeBackgroundMask();   // NativeBackground.cpp
	}
}
