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

namespace sorting
{
	namespace detail
	{
		Picker g_pickers[5];  // Inventory/Magic school, Magic effect, player side, other side, Magic type

		// RMI-004: the native container Transfer action and the end of its sequence, before their native bodies.
		void OnTransferStarted() { g_pickers[3].DetachForTransfer(); }
		void OnTransferSettled() { g_pickers[3].transfer.Settle(GetTickCount64()); }
	}

	using namespace detail;

	bool Active(int side) { return side >= 3 && side <= 4 ? g_pickers[side - 1].active.load() :
		g_pickers[0].active.load() || g_pickers[1].active.load() || g_pickers[4].active.load(); }

	void RefreshMagicHeaders(UE::UObject* a_menu)
	{
		auto& school = g_pickers[0];
		if (!a_menu || school.kind != 1 || !school.active.load() || reflect::Get(school.menu) != a_menu) return;
		auto** vm = reflect::At<UE::UObject*>(a_menu, reflect::Offset(a_menu->GetClass(), "VMagicMenuViewModel"));
		ui::Call page(vm ? *vm : nullptr, L"GetCurrentPage");
		if (!page || page.Size("ReturnValue") != 1 || !page.Run()) return;
		if (page.Get<std::uint8_t>("ReturnValue") >= 4) {
			// Active Effects has no custom pickers. Complete their existing deactivation now,
			// before the tab change renders, rather than leaving them visible until next Tick.
			for (auto& picker : g_pickers)
				if ((picker.kind == 1 || picker.kind == 2 || picker.kind == 5) && picker.active.load() &&
					reflect::Get(picker.menu) == a_menu) picker.Deactivate();
			UpdateNativeBackgroundMask();
			return;
		}
		const auto shown = [&](Picker& a_picker) {
			return (a_picker.kind == 1 || a_picker.kind == 5) && a_picker.active.load() &&
				reflect::Get(a_picker.menu) == a_menu && reflect::Get(a_picker.header);
		};
		// RMI-047: the game re-shows its own controls when it refreshes Magic. Only then (not on every
		// post-event) are our headers and sorts shown and painted again, before the frame renders.
		bool reshown = false;
		for (auto& picker : g_pickers)
			if (shown(picker)) {
				reshown |= picker.HideNativeReplacement();
				reshown |= picker.HideNativeSorts();
			}
		if (!reshown) return;
		for (auto& picker : g_pickers)
			if (shown(picker)) {
				picker.ShowOwnWidgets();
				picker.PaintSorts();   // the School host also paints the Type peer's group
			}
	}

	// RMI-047: a native tab change re-shows the game's own sort buttons. Collapse them and show the new tab's own
	// sorts in the menu's post-event, before this frame renders, not on the next Tick. Nothing changed: no widget work.
	static void RefreshTab(detail::Picker& a_picker, itemtags::Tab a_wanted)
	{
		const bool reshown = a_picker.HideNativeSorts(), switched = a_picker.tab != a_wanted;
		if (switched) a_picker.ShowTab(a_wanted);
		else if (reshown) { a_picker.ShowOwnWidgets(); a_picker.PaintSorts(); }
	}

	void RefreshInventoryHeaders(UE::UObject* a_menu)
	{
		auto& picker = g_pickers[0];
		if (!a_menu || picker.kind != 0 || !picker.active.load() || reflect::Get(picker.menu) != a_menu || !reflect::Get(picker.header)) return;
		auto** vm = reflect::At<UE::UObject*>(a_menu, reflect::Offset(a_menu->GetClass(), "VInventoryMenuViewModel"));
		auto* page = vm && *vm ? reflect::At<std::uint8_t>(*vm, reflect::Offset((*vm)->GetClass(), "CurrentPageInventory")) : nullptr;
		auto wanted = picker.tab;
		if (!page || !itemtags::TabFromPage(*page, wanted)) return;   // unreadable or not a picker tab: the per-frame Tick decides
		RefreshTab(picker, wanted);
		picker.CompensateInventorySlots();   // RMI-077: before this frame's paint arranges the game's A-Z resize
	}

	void RefreshContainerHeaders(UE::UObject* a_menu)
	{
		if (!a_menu || a_menu != container::Widget()) return;
		for (auto& picker : g_pickers) {
			if ((picker.kind != 3 && picker.kind != 4) || !picker.active.load() || !reflect::Get(picker.header) ||
				reflect::Get(picker.menu) != container::SideContent(picker.kind)) continue;
			auto wanted = picker.tab;
			// Each side has its own page byte; unreadable or not a picker tab: the per-frame Tick decides.
			if (itemtags::TabFromPage(ContainerPage(picker.kind), wanted)) RefreshTab(picker, wanted);
			picker.CompensateInventorySlots();   // RMI-077: before this frame's paint arranges the game's A-Z resize
		}
	}

	void Tick(Mode a_mode, UE::UObject* a_menu, UE::UObject* a_canvas, float a_viewportScale, UE::UObject* a_other)
	{
		static const bool s_transferHooked = (container::SetTransferHandlers(&OnTransferStarted, &OnTransferSettled), true);
		(void)s_transferHooked;
		const auto& on = mrconfig::Get().picker;   // [Picker] bInventory / bMagic / bContainer = 0: that menu stays native
		if ((a_mode == Mode::Inventory && !on.inventory) || (a_mode == Mode::Magic && !on.magic) || (a_mode == Mode::Container && !on.container))
			a_mode = Mode::None;
		if (a_mode != Mode::Magic) g_pickers[4].Tick(-1, nullptr, nullptr, 1);
		if (a_mode != Mode::Container) {
			if (a_mode != Mode::Magic) g_pickers[2].Tick(-1, nullptr, nullptr, 1);   // Magic: slot 2 hosts the Active Effects sorts
			g_pickers[3].Tick(-1, nullptr, nullptr, 1);
		}
		if (a_mode == Mode::Magic) {
			g_pickers[2].Tick(6, a_menu, a_canvas, a_viewportScale);   // Active Effects (page 4) only
			g_pickers[1].Tick(2, a_menu, a_canvas, a_viewportScale);
			g_pickers[4].Tick(5, a_menu, a_canvas, a_viewportScale);  // insert immediately before Effects
			g_pickers[0].Tick(1, a_menu, a_canvas, a_viewportScale);
		} else if (a_mode == Mode::SpellPurchase) {
			g_pickers[1].Tick(-1, nullptr, nullptr, 1);
			g_pickers[0].Tick(7, a_menu, a_canvas, a_viewportScale);
		} else if (a_mode == Mode::Inventory) {
			g_pickers[1].Tick(-1, nullptr, nullptr, 1.0f);
			g_pickers[0].Tick(0, a_menu, a_canvas, a_viewportScale);
		} else if (a_mode == Mode::Container) {
			g_pickers[1].Tick(-1, nullptr, nullptr, 1);
			g_pickers[0].Tick(-1, nullptr, nullptr, 1);
			g_pickers[2].Tick(3, a_menu, a_canvas, a_viewportScale);
			g_pickers[3].Tick(4, a_other, a_canvas, a_viewportScale);
		} else {
			g_pickers[1].Tick(-1, nullptr, nullptr, 1.0f);
			g_pickers[0].Tick(-1, nullptr, nullptr, 1.0f);
		}
		UpdateNativeBackgroundMask();
	}
}
