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

namespace sorting
{
	namespace detail
	{
		pickerpad::State g_pad;

		bool Picker::SortCarouselFocused()
		{
			// The Active Effects host (kind 6) keeps its category header hidden: our A-Z stands for the bar there (R12 log).
			auto* anchor = kind == 6 ? reflect::Get(sortHeaders[static_cast<int>(itemsort::Key::Name)]) : reflect::Get(header);
			if (!active.load() || !ui::ShownOnScreen(anchor)) return false;
			ui::Call mode(reflect::Get(menu), L"IsFilterFocused");
			if (mode && mode.Size("IsClosed") == 1) {
				if (!mode.RunGuarded() || !mode.Get<std::uint8_t>("IsClosed")) return false;
			} else {
				// Magic may not expose Inventory's Blueprint query: use the inherited focus getter.
				ui::Call nativeFocus(reflect::Get(carousel), L"GetIsFocussed");
				if (!nativeFocus || nativeFocus.Size("ReturnValue") != 1 ||
					!nativeFocus.RunGuarded() || !nativeFocus.Get<std::uint8_t>("ReturnValue")) return false;
			}
			return true;
		}

		bool Picker::Focused(const reflect::Handle& a_widget)
		{
			if (!reflect::Get(a_widget) || !SortCarouselFocused()) return false;
			ui::Call focused(reflect::Get(carousel), L"GetFocusedElement");
			return focused && focused.Size("ReturnValue") == 8 && focused.RunGuarded() &&
				focused.Get<UE::UObject*>("ReturnValue") == reflect::Get(a_widget);
		}

		void Picker::Queue(int cmd)
		{
			std::scoped_lock lock(inputLock); command = cmd; commandFromController = true;
		}

		bool Picker::Message(UINT a_message, WPARAM a_key, LPARAM a_position)
		{
			std::scoped_lock lock(inputLock);
			if (a_message == WM_LBUTTONUP && swallowUp) { swallowUp = false; return true; }
			if (a_message == WM_RBUTTONUP && swallowRightUp) { swallowRightUp = false; return true; }
			if (a_message == WM_KEYUP && a_key == VK_ESCAPE && swallowEscape) { swallowEscape = false; return true; }
			if (!inputActive) return false;
			if (a_message == WM_MOUSEMOVE) {
				mouseX = static_cast<short>(LOWORD(a_position));
				mouseY = static_cast<short>(HIWORD(a_position));
				mousePositionKnown = true;
				int hovered = -1;
				if (inputOpen) {
					for (int i = 0; i < itemtags::kMaxChoices; ++i) if (cellsPx[i].Hit(mouseX, mouseY)) { hovered = i; break; }
				}
				mouseHoverCell.store(hovered);
				return false;
			}
			if (a_message == WM_LBUTTONDBLCLK && GetTickCount64() <= doubleClickUntil) { swallowUp = true; return true; }
			if (a_message == WM_KEYDOWN && a_key == VK_ESCAPE && inputOpen) { command = -2; commandFromController = false; swallowEscape = true; return true; }
			// Owner 2026-11-07: right click on a filter opens its popup (left click = its category sort).
			if (a_message == WM_RBUTTONDOWN) {
				const double rx = static_cast<short>(LOWORD(a_position)), ry = static_cast<short>(HIWORD(a_position));
				if (kind == 6 || !headerPx.Hit(rx, ry)) return false;
				command = pickergrid::kToggleCommand; commandFromController = false;
				swallowRightUp = true;
				return true;
			}
			if (a_message != WM_LBUTTONDOWN) return false;
			commandFromController = false;
			const double x = static_cast<short>(LOWORD(a_position)), y = static_cast<short>(HIWORD(a_position));
			int sortHit = -1;
			for (int k = 0; k < kSorts && sortHit < 0; ++k) if (sortPx[k].Hit(x, y)) sortHit = k;
			if (sortHit >= 0) {
				command = itemsort::SortCommand(static_cast<itemsort::Key>(sortHit));
			}
			else if (enchantedPx.Hit(x, y)) command = enchantedfilter::kToggleCommand;
		else if (stolenPx.Hit(x, y)) command = stolenfilter::kToggleCommand;
			else if (headerPx.Hit(x, y)) command = inputOpen || !groupInput ? pickergrid::kToggleCommand : itemsort::GroupCommand(itemsort::Action::Click);
			else if (inputOpen) {
				command = -2;
				for (int i = 0; i < itemtags::kMaxChoices; ++i) if (cellsPx[i].Hit(x, y)) { command = i; break; }
			} else return false;
			swallowUp = true;
			doubleClickUntil = GetTickCount64() + GetDoubleClickTime();
			return true;
		}
	}

	using namespace detail;

	void Controller(std::uint16_t& buttons, short& x, short& y, bool connected)
	{
		if (!connected) {
			g_pad.Disconnect();
			for (auto& picker : g_pickers) {
				picker.returnToList.Cancel();
				if (picker.sortFocus >= 0) { picker.sortFocus = -1; picker.PaintSorts(); }
				if (picker.controllerFocus || picker.controllerPopup || picker.stolenFocus || picker.enchantedFocus) {
					picker.controllerFocus = picker.controllerPopup = picker.stolenFocus = picker.enchantedFocus = false; picker.PaintController();
				}
			}
			UpdateNativeBackgroundMask(); return;
		}
		const pickerpad::Input raw{ buttons, x, y };
		g_pad.Sample(raw, GetTickCount64());
		if (g_pad.pressed & pickerpad::Y)
			for (auto& picker : g_pickers)
				// Quick loot only (the player side is not shown); in Transfer and barter the game's Y works.
				if (picker.kind == 4 && picker.active.load() && ui::ShownOnScreen(reflect::Get(picker.menu)) && !container::SideShown(3)) {
					picker.quickLootWant = !picker.SortCarouselFocused();
					picker.quickLootY = GetTickCount64() + 150;   // the game's Y handler toggles Transfer sort rows itself
				}
		Picker* owner = nullptr;
		for (auto& picker : g_pickers) {
			const bool focused = picker.ControllerFocused();
			const bool stolenFocused = picker.StolenFocused();
			const bool enchantedFocused = picker.EnchantedFocused();
			const int sortFocused = picker.active.load() ? picker.SortFocused() : -1;
			if (picker.sortFocus != sortFocused) {
				picker.sortFocus = sortFocused; picker.PaintSorts();
			}
			if (picker.enchantedFocus != enchantedFocused) { picker.enchantedFocus = enchantedFocused; picker.PaintEnchanted(); }
			if (picker.stolenFocus != stolenFocused) { picker.stolenFocus = stolenFocused; picker.PaintStolen(); }
			if (picker.controllerFocus != focused) {
				picker.controllerFocus = focused;
				if (!focused && picker.controllerPopup) {
					picker.Queue(-2); picker.controllerPopup = false; picker.PaintController();
					continue; // cancellation cannot be replaced by a confirm in the same sample
				}
				picker.PaintController();
			}
			// A mouse-opened picker is modal for the pad too, but only while its menu is active.
			if (!owner && picker.active.load() && ui::ShownOnScreen(reflect::Get(picker.menu)) &&(focused || stolenFocused || enchantedFocused || sortFocused >= 0 || picker.open)) owner = &picker;
		}
		if (owner) {
			auto& picker = *owner;
			if (picker.open) {
				const bool newlyOwned = !picker.controllerPopup;
				const auto previousCursor = picker.cursor;
				picker.controllerPopup = true;
				g_pad.Consume(raw, 0xFFFF, true);
				if (g_pad.pressed & (pickerpad::B | pickerpad::Y)) picker.Queue(-3);
				else if (g_pad.pressed & pickerpad::A) picker.Queue(static_cast<int>(picker.cursor));
				else if (g_pad.pulse != pickerpad::Direction::None)
					picker.cursor = pickerpad::Move(picker.cursor, itemtags::Info(picker.view).count, g_pad.pulse);
				if (newlyOwned || picker.cursor != previousCursor) picker.PaintController();
			} else if (picker.sortFocus >= 0) {
				g_pad.Consume(raw, pickerpad::A | pickerpad::Up | pickerpad::Down | pickerpad::Left | pickerpad::Right, true);
				const auto key = static_cast<itemsort::Key>(picker.sortFocus);
				if (g_pad.pressed & pickerpad::A) picker.Queue(itemsort::SortCommand(key));
				else if (g_pad.pulse == pickerpad::Direction::Up) picker.Queue(itemsort::SortCommand(key, itemsort::Action::Ascending));
				else if (g_pad.pulse == pickerpad::Direction::Down) picker.Queue(itemsort::SortCommand(key, itemsort::Action::Descending));
				else if (g_pad.pulse == pickerpad::Direction::Left || g_pad.pulse == pickerpad::Direction::Right) {
					ui::Call navigate(reflect::Get(picker.carousel),
						g_pad.pulse == pickerpad::Direction::Left ? L"NavigateToPrevElement" : L"NavigateToNextElement");
					navigate.RunGuarded();
				}
			} else if (picker.stolenFocus || picker.enchantedFocus) {
				// A toggles the stolen-only icon; up/down are swallowed like on the picker; Y stays native (exit).
				g_pad.Consume(raw, pickerpad::A | pickerpad::Up | pickerpad::Down | pickerpad::Left | pickerpad::Right, true);
				if (g_pad.pressed & pickerpad::A) picker.Queue(picker.enchantedFocus ? enchantedfilter::kToggleCommand : stolenfilter::kToggleCommand);
				else if (g_pad.pulse == pickerpad::Direction::Left || g_pad.pulse == pickerpad::Direction::Right) {
					ui::Call navigate(reflect::Get(picker.carousel),
						g_pad.pulse == pickerpad::Direction::Left ? L"NavigateToPrevElement" : L"NavigateToNextElement");
					navigate.RunGuarded();
				}
			} else {
				// Native methods retain their original A/up/down reversal. Only our focused header owns these.
				// Only A opens the filter popup: Up/Down are swallowed and do nothing (owner 2026-11-07: Down acting as A confused).
				// Category sort (owner 2026-11-07): Up/Down on the filter group the list ascending/descending.
				g_pad.Consume(raw, pickerpad::A | pickerpad::Up | pickerpad::Down | pickerpad::Left | pickerpad::Right, true);
				if (g_pad.pressed & pickerpad::A) {
					picker.controllerPopup = true; picker.Queue(pickergrid::kToggleCommand);
				}
				else if (g_pad.pulse == pickerpad::Direction::Up && picker.GroupSorts()) picker.Queue(itemsort::GroupCommand(itemsort::Action::Ascending));
				else if (g_pad.pulse == pickerpad::Direction::Down && picker.GroupSorts()) picker.Queue(itemsort::GroupCommand(itemsort::Action::Descending));
				else if (g_pad.pulse == pickerpad::Direction::Left || g_pad.pulse == pickerpad::Direction::Right) {
					ui::Call navigate(reflect::Get(picker.carousel),
						g_pad.pulse == pickerpad::Direction::Left ? L"NavigateToPrevElement" : L"NavigateToNextElement");
					navigate.RunGuarded();
				}
			}
		}
		UpdateNativeBackgroundMask();
		const auto filtered = g_pad.Filter(raw);
		buttons = filtered.buttons; x = filtered.x; y = filtered.y;
		for (auto& picker : g_pickers) {
			if (!picker.returnToList.Pending()) continue;
			if (!core::ReturnToListAfterFilter()) { picker.returnToList.Cancel(); continue; }
			const bool focused = picker.active.load() && ui::ShownOnScreen(reflect::Get(picker.menu)) && picker.SortCarouselFocused();
			const auto action = picker.returnToList.Update(raw.buttons, focused, GetTickCount64());
			if (action == pickerpad::ReturnAction::PressY) buttons |= pickerpad::Y;
			else if (action == pickerpad::ReturnAction::TimedOut)
				logger::warn("Menus Revamped: controller return-to-list was not acknowledged by native sorting focus; press Y to exit");
		}
	}

	bool PopupOpen()
	{
		for (const auto& picker : g_pickers) if (picker.active.load() && picker.open) return true;
		return false;
	}

	bool Message(UINT a_message, WPARAM a_key, LPARAM a_position)
	{
		for (int i = 4; i >= 0; --i) if (g_pickers[i].Message(a_message, a_key, a_position)) return true;
		return false;
	}
}
