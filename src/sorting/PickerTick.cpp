#include "sorting/PickerInternal.h"

#include "core/Runtime.h"
#include "engine/ContainerMenu.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/TextUtil.h"
#include "engine/Ui.h"
#include "engine/UiText.h"
#include "sorting/GearData.h"
#include "sorting/Icons.h"

namespace sorting::detail
{
	// does an item with this mask pass this picker's choice?
	bool Picker::Passes(std::uint64_t a_mask) const
	{
		if (a_mask & kPassAll) return true;
		const auto tags = static_cast<std::uint32_t>(a_mask >> Shift());
		return tab == itemtags::Tab::AllItems ? itemtags::IncludesAll(category, choice, tags) : itemtags::Includes(choice, tags);
	}

	void Picker::Reset()
	{
		ResetShared(kind >= 3 ? static_cast<unsigned>(kind - 2) : 0);
		choice = 0; appliedChoice = 0; category = 0; appliedCategory = 0; view = tab; open = false;
		styled = false;  // the popup cells were built for the old view (stale Weapon type page, RMI-006)
		controllerFocus = controllerPopup = false; cursor = 0;
		returnToList.Cancel();
		nativeFocusWarning = false;
		paintedOpen = false; nextLayout = 0; nextHitMeasure = 0;
		widgetWarning = false;
		transfer = {};
		stolenFocus = false;
		if (auto* image = reflect::Get(headerImage)) Brush(image, itemtags::Info(tab).icons[0]);
		if (auto* frame = reflect::Get(headerFrame)) ui::RoundedBox(frame, false, kClear, kClear, 0, 0);
		PaintStolen();
		{ std::scoped_lock lock(inputLock); headerPx = {}; stolenPx = {}; popupPx = {}; command = -1; commandFromController = false; inputOpen = false; swallowUp = false; swallowEscape = false; doubleClickUntil = 0; }
	}

	void Picker::Deactivate()
	{
		if (active.exchange(false)) {
			Reset();
			tabSet = false;
			stolenOnly = false; PaintStolen();  // the menu closed: the next one starts unfiltered
			if (auto* s = reflect::Get(stolenHeader)) ui::Visible(s, false);
			if (auto* h = reflect::Get(header)) ui::Visible(h, false);
			if (auto* p = reflect::Get(popup)) ui::Visible(p, false);
		}
		std::scoped_lock lock(inputLock); inputActive = false; inputOpen = false; command = -1;
	}

	// a_kind: 0 inventory tabs, 1 magic school, 2 magic effect, -1 not shown
	void Picker::Tick(int a_kind, UE::UObject* a_menu, UE::UObject* a_canvas, float a_viewportScale)
	{
		bool show = false;
		itemtags::Tab wanted = itemtags::Tab::Gear;
		if (a_kind >= 0 && a_menu && a_canvas) {
			if (a_kind >= 3) {
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
				show = page && page.Size("ReturnValue") == 1 && page.Run() && page.Get<std::uint8_t>("ReturnValue") < 4;
				wanted = a_kind == 1 ? itemtags::Tab::MagicSchool : itemtags::Tab::MagicEffect;
			}
		}
		if (!show) { Deactivate(); kind = -1; return; }
		if (kind != a_kind) {  // another menu: forget everything and rebuild
			if (kind >= 0) { stolenOnly = false; Reset(); tabSet = false; }
			kind = a_kind;
		}
		active.store(true);  // page identity is independent of whether its new widgets have finished layout
		if (!tabSet || tab != wanted) {  // another tab: its own choices and filter
			if (tabSet) { const auto hold = transfer; Reset(); transfer = hold; }  // the RMI-004 Transfer hold may span a page change
			tab = wanted; view = wanted; styled = false;
		}
		tabSet = true;
		// The game re-shows the native School wrapper when its filters update. Collapse it
		// on every tick (not only the 150 ms layout pass) so its glyph never renders beside ours.
		if (auto* native = reflect::Get(nativeSchool)) {
			ui::Call visible(native, L"IsVisible");
			if (!visible || visible.Size("ReturnValue") != 1 || !visible.Run() || visible.Get<bool>("ReturnValue")) {
				ui::Call hide(native, L"SetVisibility");
				const bool hidden = hide && hide.Size("InVisibility") == 1 &&
					hide.Set("InVisibility", static_cast<std::uint8_t>(1)) && hide.Run();
				if (!hidden) logger::warn("Menus Revamped: SO-SCHOOL-FLASH-20261025 could not collapse the native School wrapper");
			}
		}
		if (reflect::Get(menu) != a_menu || reflect::Get(canvas) != a_canvas) {
			stolenOnly = false; Reset();
			for (const auto& handle : { popup, header, stolenHeader }) if (auto* old = reflect::Get(handle)) { ui::Call remove(old, L"RemoveFromParent"); remove.Run(); }
			menu = reflect::Hold(a_menu); popup = {}; paperSlot = {}; header = {}; carousel = {}; nativeSchool = {}; nativePeers.clear();
			stolenHeader = {}; stolenImage = {}; stolenFrame = {};
			canvas = reflect::Hold(a_canvas); nextLayout = 0;
		}
		if (!reflect::Get(popup) && !BuildPopup(a_canvas)) {
			if (!widgetWarning) { logger::warn("gear piece filter: could not construct picker widgets"); widgetWarning = true; }
			return;
		}
		if (!styled) { StyleCells(); styled = true; }
		const auto now = GetTickCount64();
		if (!fontsReady && now >= nextFontTry) {
			nextFontTry = now + 1000;
			fontsReady = PickerFont(reflect::Get(title), 18);
			for (const auto& label : labels) fontsReady = PickerFont(reflect::Get(label), 14) && fontsReady;
			if (!fontsReady && !fontWarning) {
				logger::warn("Menus Revamped: item/spell Kingthings font unavailable; retrying");
				fontWarning = true;
			}
		}
		if (now >= nextLayout) {
			nextLayout = now + 150;
			auto find = [&](UE::UObject* owner, std::string_view name) {
				return kind >= 3 ? container::FindWidget(owner, name) : ui::FindInTree(owner, name);
			};
			auto* iconBox = find(a_menu, "FilterIconBox");
			if (!iconBox && kind >= 3) iconBox = find(a_menu, "inv_cont_filter");
			auto* panel = iconBox ? find(iconBox, "ScrollBox") : nullptr;  // FilterIconBox is a user widget wrapping the real panel
			auto* headerWidget = reflect::Get(header);
			ui::Call parent(headerWidget, L"GetParent");
			const bool attached = parent && parent.Run() && parent.Get<UE::UObject*>("ReturnValue") == panel;
			if (!panel) {
				if (kind >= 3 && !widgetWarning) {
					logger::warn("Menus Revamped: container side {} has no supported native filter ScrollBox; picker not attached", kind);
					widgetWarning = true;
				}
				return;
			}
			if (!attached && transfer.Held() && !transfer.MayReinsert(now, NativeNameShown(panel))) return;
			if (!attached && !InsertHeader(panel, iconBox)) {
				if (!widgetWarning) { logger::warn("Menus Revamped: native controller focus-only header registration/layout unsupported"); widgetWarning = true; }
				return;
			}
			ui::Visible(reflect::Get(header), true);
			if (auto* native = reflect::Get(nativeSchool)) ui::Visible(native, false);  // the game may show it again when its filters update
			if (auto* stolen = reflect::Get(stolenHeader)) {
				ui::Visible(stolen, true);
				double sx = 0, sy = 0, sw = 0, sh = 0;
				if (ui::Measure(stolen, sx, sy, sw, sh) && sw > 0 && sh > 0) {
					std::scoped_lock lock(inputLock);
					stolenPx = { sx * a_viewportScale, sy * a_viewportScale, sw * a_viewportScale, sh * a_viewportScale };
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
		int cmd = -1;
		bool fromController = false;
		{ std::scoped_lock lock(inputLock); cmd = command; fromController = commandFromController; command = -1; commandFromController = false; }
		if (cmd == stolenfilter::kToggleCommand) {
			stolenOnly = !stolenOnly; open = false;
			PaintStolen();
		}
		else if (cmd == pickergrid::kToggleCommand) {
			open = !open; nextHitMeasure = 0;
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
		if (!open) controllerPopup = false;
		const std::uint8_t popupVisibility = open ? 0 : 1;  // visible background blocks hover/click hit tests on rows below it
		if (open != paintedOpen) {
			ui::CallFirst(reflect::Get(popup), L"SetVisibility", &popupVisibility, sizeof(popupVisibility));
			paintedOpen = open;
		}

		if (cmd >= 0) Brush(reflect::Get(headerImage), HeaderIcon());
		if (cmd != -1) PaintController();
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
		// Never hold the window-message mutex across ProcessEvent / geometry calls.
		{
			std::scoped_lock lock(inputLock);
			inputActive = true; inputOpen = open;
			if (measure) { popupPx = popupRect; std::copy(std::begin(cellRects), std::end(cellRects), cellsPx); }
		}
		active.store(true);
	}
}
