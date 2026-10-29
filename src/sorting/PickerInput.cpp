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

namespace gearfilter
{
	namespace detail
	{
		sortpad::State g_pad;

		bool Picker::SortCarouselFocused()
		{
			if (!active.load() || !ui::ShownOnScreen(reflect::Get(header))) return false;
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

		bool Picker::Contains(double a_x, double a_y)
		{
			std::scoped_lock lock(inputLock);
			return inputActive && (swallowUp || headerPx.Hit(a_x, a_y) || stolenPx.Hit(a_x, a_y) || (inputOpen && popupPx.Hit(a_x, a_y)));
		}

		bool Picker::Message(UINT a_message, WPARAM a_key, LPARAM a_position)
		{
			std::scoped_lock lock(inputLock);
			if (a_message == WM_LBUTTONUP && swallowUp) { swallowUp = false; return true; }
			if (a_message == WM_KEYUP && a_key == VK_ESCAPE && swallowEscape) { swallowEscape = false; return true; }
			if (!inputActive) return false;
			if (a_message == WM_LBUTTONDBLCLK && GetTickCount64() <= doubleClickUntil) { swallowUp = true; return true; }
			if (a_message == WM_KEYDOWN && a_key == VK_ESCAPE && inputOpen) { command = -2; commandFromController = false; swallowEscape = true; return true; }
			if (a_message != WM_LBUTTONDOWN) return false;
			commandFromController = false;
			const double x = static_cast<short>(LOWORD(a_position)), y = static_cast<short>(HIWORD(a_position));
			if (stolenPx.Hit(x, y)) command = stolenfilter::kToggleCommand;
			else if (headerPx.Hit(x, y)) command = pickergrid::kToggleCommand;
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
				if (picker.controllerFocus || picker.controllerPopup || picker.stolenFocus) {
					picker.controllerFocus = picker.controllerPopup = picker.stolenFocus = false; picker.PaintController();
				}
			}
			UpdateNativeBackgroundMask(); return;
		}
		const sortpad::Input raw{ buttons, x, y };
		g_pad.Sample(raw, GetTickCount64());
		Picker* owner = nullptr;
		for (auto& picker : g_pickers) {
			const bool focused = picker.ControllerFocused();
			const bool stolenFocused = picker.StolenFocused();
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
			if (!owner && picker.active.load() && ui::ShownOnScreen(reflect::Get(picker.menu)) && (focused || stolenFocused || picker.open)) owner = &picker;
		}
		if (owner) {
			auto& picker = *owner;
			if (picker.open) {
				const bool newlyOwned = !picker.controllerPopup;
				const auto previousCursor = picker.cursor;
				picker.controllerPopup = true;
				g_pad.Consume(raw, 0xFFFF, true);
				if (g_pad.pressed & (sortpad::B | sortpad::Y)) picker.Queue(-3);
				else if (g_pad.pressed & sortpad::A) picker.Queue(static_cast<int>(picker.cursor));
				else if (g_pad.pulse != sortpad::Direction::None)
					picker.cursor = sortpad::Move(picker.cursor, itemtags::Info(picker.view).count, g_pad.pulse);
				if (newlyOwned || picker.cursor != previousCursor) picker.PaintController();
			} else if (picker.stolenFocus) {
				// A toggles the stolen-only icon; up/down are swallowed like on the picker; Y stays native (exit).
				g_pad.Consume(raw, sortpad::A | sortpad::Up | sortpad::Down | sortpad::Left | sortpad::Right, true);
				if (g_pad.pressed & sortpad::A) picker.Queue(stolenfilter::kToggleCommand);
				else if (g_pad.pulse == sortpad::Direction::Left || g_pad.pulse == sortpad::Direction::Right) {
					ui::Call navigate(reflect::Get(picker.carousel),
						g_pad.pulse == sortpad::Direction::Left ? L"NavigateToPrevElement" : L"NavigateToNextElement");
					navigate.RunGuarded();
				}
			} else {
				// Native methods retain their original A/up/down reversal. Only our focused header owns these.
				g_pad.Consume(raw, sortpad::A | sortpad::Up | sortpad::Down | sortpad::Left | sortpad::Right, true);
				if ((g_pad.pressed & sortpad::A) || g_pad.pulse == sortpad::Direction::Up || g_pad.pulse == sortpad::Direction::Down) {
					picker.controllerPopup = true; picker.Queue(pickergrid::kToggleCommand);
				}
				else if (g_pad.pulse == sortpad::Direction::Left || g_pad.pulse == sortpad::Direction::Right) {
					ui::Call navigate(reflect::Get(picker.carousel),
						g_pad.pulse == sortpad::Direction::Left ? L"NavigateToPrevElement" : L"NavigateToNextElement");
					navigate.RunGuarded();
				}
			}
		}
		UpdateNativeBackgroundMask();
		const auto filtered = g_pad.Filter(raw);
		buttons = filtered.buttons; x = filtered.x; y = filtered.y;
		for (auto& picker : g_pickers) {
			if (!picker.returnToList.Pending()) continue;
			if (!sorting::ReturnToListAfterFilter()) { picker.returnToList.Cancel(); continue; }
			const bool focused = picker.active.load() && ui::ShownOnScreen(reflect::Get(picker.menu)) && picker.SortCarouselFocused();
			const auto action = picker.returnToList.Update(raw.buttons, focused, GetTickCount64());
			if (action == sortpad::ReturnAction::PressY) buttons |= sortpad::Y;
			else if (action == sortpad::ReturnAction::TimedOut)
				logger::warn("Sorting Overhaul: controller return-to-list was not acknowledged by native sorting focus; press Y to exit");
		}
	}

	bool PopupOpen()
	{
		for (const auto& picker : g_pickers) if (picker.active.load() && picker.open) return true;
		return false;
	}

	bool Contains(double a_x, double a_y)
	{
		for (auto& picker : g_pickers) if (picker.Contains(a_x, a_y)) return true;
		return false;
	}

	bool Message(UINT a_message, WPARAM a_key, LPARAM a_position)
	{
		for (int i = 3; i >= 0; --i) if (g_pickers[i].Message(a_message, a_key, a_position)) return true;
		return false;
	}
}
