#include "sorting/PickerInternal.h"

#include "core/Config.h"
#include "core/Runtime.h"
#include "engine/ContainerMenu.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/TextUtil.h"
#include "engine/Ui.h"
#include "engine/UiText.h"
#include "sorting/GearData.h"
#include "sorting/Icons.h"
#include "sorting/MenuSkin.h"

namespace sorting::detail
{
	bool Picker::HideNativeReplacement()
	{
		if (!active.load()) return false;
		if (auto* native = reflect::Get(nativeSchool)) {
			ui::Call visible(native, L"IsVisible");
			if (!visible || visible.Size("ReturnValue") != 1 || !visible.Run() || visible.Get<bool>("ReturnValue")) {
				ui::Call hide(native, L"SetVisibility");
				const bool hidden = hide && hide.Size("InVisibility") == 1 &&
					hide.Set("InVisibility", static_cast<std::uint8_t>(1)) && hide.Run();
				if (!hidden) logger::warn("Menus Revamped RMI-018 R2: native Magic sort replacement could not collapse its wrapper");
				return true;
			}
		}
		return false;
	}

	// does an item with this mask pass this picker's choice?
	bool Picker::Passes(std::uint64_t a_mask) const
	{
		if (kind == 5) return magicrows::TypePasses(a_mask, choice);
		if (a_mask & kPassAll) return true;
		const auto tags = static_cast<std::uint32_t>(a_mask >> Shift());
		return tab == itemtags::Tab::AllItems ? itemtags::IncludesAll(category, choice, tags) : itemtags::Includes(choice, tags);
	}

	void Picker::Reset()
	{
		ResetShared(kind >= 3 && kind <= 4 ? static_cast<unsigned>(kind - 2) : 0);
		choice = 0; appliedChoice = 0; category = 0; appliedCategory = 0; view = tab; open = false;
		mouseHoverCell.store(-1);
		mouseInputActive.store(false);
		{ std::scoped_lock lock(inputLock); mousePositionKnown = false; }
		styled = false;  // the popup cells were built for the old view (stale Weapon type page, RMI-006)
		controllerFocus = controllerPopup = false; cursor = 0;
		returnToList.Cancel();
		nativeFocusWarning = false;
		paintedOpen = false; nextLayout = 0; nextHitMeasure = 0;
		widgetWarning = false;
		transfer = {};
		stolenFocus = false; enchantedFocus = false;
		ClearHitAreas();
		BrushHeader(itemtags::Info(tab).icons[0]);
		if (auto* frame = reflect::Get(headerFrame)) ui::RoundedBox(frame, false, kClear, kClear, 0, 0);
		PaintStolen();
		PaintEnchanted();
		{ std::scoped_lock lock(inputLock); headerPx = {}; stolenPx = {}; enchantedPx = {}; popupPx = {}; command = -1; commandFromController = false; inputOpen = false; swallowUp = false; swallowEscape = false; doubleClickUntil = 0; }
	}

	void Picker::Deactivate()
	{
		if (active.exchange(false)) {
			Reset();
			tabSet = false;
			if (kind == 0) HideInventoryHeaderLayout();
			stolenOnly = false; PaintStolen();
			enchantedOnly = false; PaintEnchanted();
			if (auto* e = reflect::Get(enchantedHeader)) ui::Visible(e, false);  // the menu closed: the next one starts unfiltered
			if (auto* s = reflect::Get(stolenHeader)) ui::Visible(s, false);
			if (auto* h = reflect::Get(header)) ui::Visible(h, false);
			if (auto* p = reflect::Get(popup)) ui::Visible(p, false);
			for (const auto& sort : sortHeaders) if (auto* w = reflect::Get(sort)) ui::Visible(w, false);
			paintedSlot = -1;
			// Magic spell pages -> Active Effects: the game's own per-page rule shows its buttons again (we collapsed A-Z/Cost).
			auto* shown = reflect::Get(menu);
			if ((kind == 1 || kind == 6) && !nativeSorts.empty() && reflect::Usable(shown) && ui::ShownOnScreen(shown)) {
				auto** vm = reflect::At<UE::UObject*>(shown, reflect::Offset(shown->GetClass(), "VMagicMenuViewModel"));
				ui::Call page(vm ? *vm : nullptr, L"GetCurrentPage");
				if (page && page.Size("ReturnValue") == 1 && page.Run()) {
					ui::Call restore(shown, L"UpdateFiltersToDisplay");
					if (restore && restore.Size("Page") == 1 && restore.Set("Page", page.Get<std::uint8_t>("ReturnValue"))) restore.Run();
					else logger::warn("Menus Revamped: Magic native sort buttons could not be restored for this page");
				}
			}
		}
		std::scoped_lock lock(inputLock); inputActive = false; inputOpen = false; command = -1;
	}

	// Another tab: its own choices, then its own sorts shown and painted in the same frame (Tick, and the
	// Inventory post-event refresh, RMI-047).
	void Picker::ShowTab(itemtags::Tab a_tab)
	{
		inventoryHeaderMeasured = false;
		if (kind == 0) {
			inventoryFillPending = true;
			SetInventoryFillOpacity(0.0f);
		}
		if (tabSet) { const auto hold = transfer; Reset(); transfer = hold; }  // the RMI-004 Transfer hold may span a page change
		tab = a_tab; view = a_tab; styled = false;
		ShowOwnWidgets();
		if (kind == 0 && reflect::Get(header) &&
			LayoutInventoryHeaders(reflect::Get(menu),reflect::Get(canvas),false))
			nextLayout=GetTickCount64()+16;
		PaintSorts();   // the new tab's sorts and its remembered choice, in the same frame
		tabSet = true;
	}

	// a_kind: 0 inventory, 1 school, 2 effect, 3/4 container sides, 5 Magic type, -1 hidden
	void Picker::Tick(int a_kind, UE::UObject* a_menu, UE::UObject* a_canvas, float a_viewportScale)
	{
		bool show = false;
		itemtags::Tab wanted = itemtags::Tab::Gear;
		if (a_kind >= 0 && a_menu && a_canvas) {
			if (a_kind >= 3 && a_kind <= 4) {
				show = ui::ShownOnScreen(a_menu);
				wanted = itemtags::Tab::AllItems;  // full tree when the side's page cannot be read
				// RMI-006: follow the side's native tab like the Inventory (Misc tab -> Misc choices).
				itemtags::TabFromPage(ContainerPage(a_kind), wanted);
			} else if (a_kind == 0) {
				auto** vm = reflect::At<UE::UObject*>(a_menu, reflect::Offset(a_menu->GetClass(), "VInventoryMenuViewModel"));
				if (vm && *vm) {
					auto* page = reflect::At<std::uint8_t>(*vm, reflect::Offset((*vm)->GetClass(), "CurrentPageInventory"));
					show = page && itemtags::TabFromPage(*page, wanted);
				}
			} else {
				// Visibility follows the native tab, never the filtered rows. Active effects is page 4.
				auto** vm = reflect::At<UE::UObject*>(a_menu, reflect::Offset(a_menu->GetClass(), "VMagicMenuViewModel"));
				ui::Call page(vm ? *vm : nullptr, L"GetCurrentPage");
				const bool read = page && page.Size("ReturnValue") == 1 && page.Run();
				const auto  now = read ? page.Get<std::uint8_t>("ReturnValue") : std::uint8_t{ 0xFF };
				show = read && (a_kind == 6 ? now == 4 : now < 4);   // kind 6: Active Effects; the pickers: spell pages
				if (show && now < 4) magicPage = now;   // the own Magic sorts are kept per spell page
				wanted = a_kind == 1 || a_kind == 6 ? itemtags::Tab::MagicSchool : a_kind == 5 ? itemtags::Tab::MagicType : itemtags::Tab::MagicEffect;
			}
		}
		if (!show) { Deactivate(); kind = -1; return; }
		if (kind != a_kind) {  // another menu: forget everything and rebuild
			if (kind >= 0) { enchantedOnly = false; stolenOnly = false; Reset(); tabSet = false; }
			kind = a_kind;
			if (kind != 0) ClearInventoryHeaderLayout();
		}
		active.store(true);  // page identity is independent of whether its new widgets have finished layout
		SeedSorts();   // first time this kind is seen: every tab starts with the INI default (bCategorySort), before any paint
		if (!tabSet || tab != wanted) ShowTab(wanted);   // another tab: its own choices and filter
		if (OwnSorts() && SortSlot() != paintedSlot) { ShowOwnWidgets(); PaintSorts(); }   // a Magic page change keeps the tab
		// The game re-shows native School/ItemCategory wrappers when its filters update. Collapse them
		// on every tick (not only the 150 ms layout pass) so its glyph never renders beside ours.
		HideNativeReplacement();
		HideNativeSorts();
		ShowQuickLootHint();
		if (reflect::Get(menu) != a_menu || reflect::Get(canvas) != a_canvas) {
			ClearInventoryHeaderLayout();
			enchantedOnly = false; stolenOnly = false; Reset();
			for (const auto& handle : { popup, header, stolenHeader, enchantedHeader }) if (auto* old = reflect::Get(handle)) { ui::Call remove(old, L"RemoveFromParent"); remove.Run(); }
			for (int k = 0; k < kSorts; ++k) {
				if (auto* old = reflect::Get(sortHeaders[k])) { ui::Call remove(old, L"RemoveFromParent"); remove.Run(); }
				sortHeaders[k] = {}; sortImages[k] = {}; sortFrames[k] = {}; sortArrows[k] = {}; sortBacks[k] = {};
			}
			nativeSorts.clear(); sortFocus = -1;
			menu = reflect::Hold(a_menu); popup = {}; paperSlot = {}; header = {}; carousel = {}; nativeSchool = {}; nativePeers.clear();
			stolenHeader = {}; stolenImage = {}; stolenFrame = {};
			enchantedHeader = {}; enchantedImage = {}; enchantedFrame = {};
			canvas = reflect::Hold(a_canvas); nextLayout = 0;
		}
		if (!reflect::Get(popup) && !BuildPopup(a_canvas)) {
			if (!widgetWarning) { logger::warn("Menus Revamped: exclusive picker popup could not be constructed"); widgetWarning = true; }
			return;
		}
		if (!styled) { StyleCells(); styled = true; }
		const auto now = GetTickCount64();
		if (!fontsReady && now >= nextFontTry) {
			nextFontTry = now + 1000;
			const auto& style = uistyle::Get(mrconfig::Get().stylePreset).picker;
			fontsReady = PickerFont(reflect::Get(title), style.titleFontSize);
			for (const auto& label : labels) fontsReady = PickerFont(reflect::Get(label), style.labelFontSize) && fontsReady;
			if (!fontsReady && !fontWarning) {
				logger::warn("Menus Revamped: item/spell Kingthings font unavailable; retrying");
				fontWarning = true;
			}
		}
		if (kind == 0) {
			menuskin::RefreshInventoryTitle();
			menuskin::RefreshButtonRules();
		}
		if (now >= nextLayout) {
			nextLayout = now + 150;
			auto find = [&](UE::UObject* owner, std::string_view name) {
				return kind >= 3 && kind <= 4 ? container::FindWidget(owner, name) : ui::FindInTree(owner, name);
			};
			auto* iconBox = find(a_menu, "FilterIconBox");
			if (!iconBox && kind >= 3 && kind <= 4) iconBox = find(a_menu, "inv_cont_filter");
			auto* panel = iconBox ? find(iconBox, "ScrollBox") : nullptr;  // FilterIconBox is a user widget wrapping the real panel
			auto* headerWidget = reflect::Get(header);
			ui::Call parent(headerWidget, L"GetParent");
			const bool attached = parent && parent.Run() && parent.Get<UE::UObject*>("ReturnValue") == panel;
			if (!panel) {
				ClearHitAreas();
				if (kind >= 3 && kind <= 4 && !widgetWarning) {
					logger::warn("Menus Revamped: container side {} has no supported native filter ScrollBox; picker not attached", kind);
					widgetWarning = true;
				}
				return;
			}
			if (!attached && transfer.Held() && !transfer.MayReinsert(now, NativeNameShown(panel))) { ClearHitAreas(); return; }
			if (!attached && !InsertHeader(panel, iconBox)) {
				ClearHitAreas();
				if (!widgetWarning) { logger::warn("Menus Revamped: native-width picker header registration/layout unsupported"); widgetWarning = true; }
				return;
			}
			const bool justAttached = !attached;
			if (justAttached) { HideNativeSorts(); PaintSorts(); }
			ShowOwnWidgets();   // every custom widget of the bar in this one pass (owner 2026-11-07)
			if (OwnSorts()) {
				if (kind == 0) LayoutInventoryHeaders(a_menu, a_canvas);
				if (kind == 0 && inventoryFillPending) nextLayout=now+16;
				WatchRefills(*this);
				if (justAttached) Republish(*this);   // the opening's first refill came before the hook: order it now
				if (kind == 0 && !inventoryHeaderReady) {
					nextLayout=now+16;
					ClearHitAreas();
					return;
				}
				for (int k = 0; k < kSorts; ++k) {
					Rect r;
					auto* sort = reflect::Get(sortHeaders[k]);
					if (sort && Offers(static_cast<itemsort::Key>(k)) && ui::Measure(sort, r.x, r.y, r.w, r.h) && r.w > 0 && r.h > 0) {
						r.x *= a_viewportScale; r.y *= a_viewportScale; r.w *= a_viewportScale; r.h *= a_viewportScale;
					} else r = {};
					std::scoped_lock lock(inputLock);
					sortPx[k] = r;
				}
			}
			if (auto* native = reflect::Get(nativeSchool)) ui::Visible(native, false);  // the game may show it again when its filters update
			if (auto* stolen = reflect::Get(stolenHeader)) {
				double sx = 0, sy = 0, sw = 0, sh = 0;
				if (ui::Measure(stolen, sx, sy, sw, sh) && sw > 0 && sh > 0) {
					std::scoped_lock lock(inputLock);
					stolenPx = { sx * a_viewportScale, sy * a_viewportScale, sw * a_viewportScale, sh * a_viewportScale };
				}
			}
			if (auto* enchanted = reflect::Get(enchantedHeader)) {
				double sx = 0, sy = 0, sw = 0, sh = 0;
				if (ui::Measure(enchanted, sx, sy, sw, sh) && sw > 0 && sh > 0) {
					std::scoped_lock lock(inputLock);
					enchantedPx = { sx * a_viewportScale, sy * a_viewportScale, sw * a_viewportScale, sh * a_viewportScale };
				}
			}
			double x = 0, y = 0, w = 0, h = 0;
			if (ui::Measure(reflect::Get(header), x, y, w, h) && w > 0 && h > 0) {
				double cx = 0, cy = 0, cw = 0, ch = 0;
				ui::Measure(a_canvas, cx, cy, cw, ch);
				const double popupHeight = pickergrid::Height(itemtags::Info(view).count);
				ui::Vec2(reflect::Get(popupSlot), L"SetPosition",
					pickergrid::PopupLeft(x - cx, w, cw, itemtags::Info(view).count),
					pickergrid::Position(y - cy + h + 5, popupHeight, ch));
				std::scoped_lock lock(inputLock);
				headerPx = { x * a_viewportScale, y * a_viewportScale, w * a_viewportScale, h * a_viewportScale };
			}
		}
		if (quickLootY && now >= quickLootY) {
			quickLootY = 0;
			if (SortCarouselFocused() != quickLootWant)
				if (auto* side = container::SideContent(kind)) {
					ui::Call focus(side, L"UpdateFilterFocus");
					if (focus && focus.Size("InIsFilterFocused") == 1 && focus.Set("InIsFilterFocused", quickLootWant)) focus.Run();
				}
		}
		int cmd = -1;
		bool fromController = false;
		{ std::scoped_lock lock(inputLock); cmd = command; fromController = commandFromController; command = -1; commandFromController = false; }
		if (pendingRepublish && OwnSorts()) {   // the classification landed for rows the category sort could not place
			pendingRepublish = false;
			Republish(*this);
		}
		if (itemsort::IsGroupCommand(cmd)) {
			if (auto* host = SortHost(); host && host->OwnSorts()) {
				auto& state = host->SortAt(host->SortSlot());
				state = itemsort::ApplyGroup(state, kind, itemsort::GroupCommandAction(cmd));
				host->PaintSorts();   // the host's buttons go plain; every filter of the host repaints its category sort
				Republish(*host);
			}
		}
		else if (itemsort::IsSortCommand(cmd)) {
			auto& state = SortAt(SortSlot());
			state = itemsort::Apply(state, SortKeys(), itemsort::SortCommandKey(cmd), itemsort::SortCommandAction(cmd));
			PaintSorts();
			Republish(*this);
		}
		else if (cmd == enchantedfilter::kToggleCommand) {
			enchantedOnly = !enchantedOnly; open = false;
			PaintEnchanted();
		}
		else if (cmd == stolenfilter::kToggleCommand) {
			stolenOnly = !stolenOnly; open = false;
			PaintStolen();
		}
		else if (cmd == pickergrid::kToggleCommand) {
			if (open) open = false; else OpenPopup();
			nextHitMeasure = 0;
			if (open && view != tab) { view = tab; styled = false; }  // always open on the top level
			cursor = Highlight() < itemtags::Info(tab).count ? Highlight() : 0;
		}
		else if (pickergrid::IsSelection(cmd, itemtags::Info(view).count)) {
			const auto next = pickernav::Choose(tab, view, category, choice, static_cast<unsigned>(cmd));
			if (view != next.view) { view = next.view; styled = false; nextHitMeasure = 0; nextLayout = 0; }
			category = next.category; choice = next.choice; cursor = next.cursor;
			open = !next.close;
			if (pickerpad::ShouldReturnToList(core::ReturnToListAfterFilter(), fromController, next.close))
				returnToList.Request();
		}
		else if (cmd == -3) {
			const auto next = pickernav::Back(tab, view, category, choice);
			if (view != next.view) { view = next.view; styled = false; nextHitMeasure = 0; nextLayout = 0; }
			cursor = next.cursor;
			if (next.close) open = false;
		}
		else if (cmd == -2) open = false;
		// A nested view changed after the normal style pass. Update visible cells
		// before measuring input regions, not a tick later with stale parent cells.
		if (!styled) { StyleCells(); styled = true; }
		if (!open) { controllerPopup = false; mouseHoverCell.store(-1); }
		const std::uint8_t popupVisibility = open ? 0 : 1;  // visible background blocks hover/click hit tests on rows below it
		if (open != paintedOpen) {
			ui::CallFirst(reflect::Get(popup), L"SetVisibility", &popupVisibility, sizeof(popupVisibility));
			paintedOpen = open;
		}

		if (cmd >= 0) BrushHeader(HeaderIcon());
		Rect popupRect, cellRects[itemtags::kMaxChoices];
		const bool measure = open && now >= nextHitMeasure;
		if (measure) {
			nextHitMeasure = now + 100;
			ui::Measure(reflect::Get(popup), popupRect.x, popupRect.y, popupRect.w, popupRect.h);
			if (popupRect.w <= 0 || popupRect.h <= 0) nextHitMeasure = 0;  // wait for the first Slate layout
			popupRect.x *= a_viewportScale; popupRect.y *= a_viewportScale; popupRect.w *= a_viewportScale; popupRect.h *= a_viewportScale;
			for (unsigned i = 0; i < itemtags::Info(view).count; ++i) {
				auto& rect = cellRects[i]; ui::Measure(reflect::Get(cells[i]), rect.x, rect.y, rect.w, rect.h);
				rect.x *= a_viewportScale; rect.y *= a_viewportScale; rect.w *= a_viewportScale; rect.h *= a_viewportScale;
			}
		}
		const bool groupSorts = GroupSorts();
		// Never hold the window-message mutex across ProcessEvent / geometry calls.
		{
			std::scoped_lock lock(inputLock);
			inputActive = true; inputOpen = open; groupInput = groupSorts;
			if (measure) { popupPx = popupRect; std::copy(std::begin(cellRects), std::end(cellRects), cellsPx); }
			// A stationary pointer may now cover a different cell (or none) after reflow.
			if (measure) {
				int hovered = -1;
				if (mousePositionKnown)
					for (int i = 0; i < itemtags::kMaxChoices; ++i) if (cellsPx[i].Hit(mouseX, mouseY)) { hovered = i; break; }
				mouseHoverCell.store(hovered);
			}
		}
		const int hoveredCell = mouseHoverCell.load();
		const bool mouseInput = mouseInputActive.load();
		if (cmd != -1 || hoveredCell != paintedMouseHoverCell || mouseInput != paintedMouseInputActive) {
			PaintController();
			paintedMouseHoverCell = hoveredCell;
			paintedMouseInputActive = mouseInput;
		}
		active.store(true);
	}
}
