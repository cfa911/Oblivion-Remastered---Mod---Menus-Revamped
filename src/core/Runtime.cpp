// Menus Revamped: single discovery, window-message and UI lifecycle.
#include "core/Runtime.h"
#include "sorting/Picker.h"
#include "engine/ContainerMenu.h"
#include "engine/Menus.h"
#include "engine/Reflect.h"
#include "engine/Ui.h"
#include "core/Config.h"
#include "hover/HoverFix.h"
#include "search/Search.h"
#include "engine/QuestsMenu.h"
#include "keyboard/Keyboard.h"

namespace core
{
	namespace
	{
		WNDPROC g_previous = nullptr;
		DWORD g_thread = 0;
		bool g_inside = false;
		std::atomic<bool> g_keyboardOpen{ false };
		constexpr UINT_PTR kTimer = 0x4D52;

		LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM key, LPARAM position)
		{
			if (message == WM_TIMER && key == kTimer && GetCurrentThreadId() == g_thread) { Tick(); return 0; }
			if (sorting::PopupOpen() && sorting::Message(message, key, position)) return 0;
			LRESULT result = 0;
			if (search::HandleMessage(window, message, key, position, result)) return result;
			if (sorting::Message(message, key, position)) return 0;
			return CallWindowProcW(g_previous, window, message, key, position);
		}

		BOOL CALLBACK FindWindow(HWND window, LPARAM result)
		{
			DWORD process = 0; GetWindowThreadProcessId(window, &process);
			wchar_t name[64]{}; GetClassNameW(window, name, 64);
			if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) || std::wcscmp(name, L"UnrealWindow")) return TRUE;
			*reinterpret_cast<HWND*>(result) = window;
			return FALSE;
		}

		void WatchWindow()
		{
			static ULONGLONG next = 0;
			if (g_previous || GetTickCount64() < next) return;
			next = GetTickCount64() + 1000;
			HWND window = nullptr;
			EnumWindows(&FindWindow, reinterpret_cast<LPARAM>(&window));
			if (!window) return;
			g_previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc)));
			if (!g_previous) return;
			search::SetWindow(window);
			if (GetWindowThreadProcessId(window, nullptr) == g_thread) SetTimer(window, kTimer, 8, nullptr);
		}
	}

	bool ReturnToListAfterFilter() { return mrconfig::Get().returnToListAfterFilter; }
	bool ControllerKeyboardOpen() { return g_keyboardOpen.load(); }
	void PublishInputState() { g_keyboardOpen.store(keyboard::Taking()); }

	void Tick()
	{
		if (g_inside) return;
		if (!g_thread) g_thread = GetCurrentThreadId();
		if (GetCurrentThreadId() != g_thread) return;
		g_inside = true;
		struct Exit { ~Exit() { g_inside = false; } } exit;
		WatchWindow();
		static ULONGLONG next = 0;
		if (GetTickCount64() >= next) {
			next = GetTickCount64() + 200;
			if (!reflect::Ok()) reflect::SelfCheck();
			if (reflect::Ok()) { menus::Tick(); container::Tick(); quests::Tick(); }
		}
		if (!reflect::Ok()) return;
		if (container::Open()) {
			sorting::Tick(sorting::Mode::Container, container::SideContent(3),
				ui::TreeRootCanvas(container::Widget()), ui::ViewportScale(), container::SideContent(4));
		} else {
			const auto menu = menus::Active();
			if (menu == menus::Menu::kInventory || menu == menus::Menu::kMagic) {
				auto* widget = menus::Widget(menu);
				sorting::Tick(menu == menus::Menu::kMagic ? sorting::Mode::Magic : sorting::Mode::Inventory,
					widget, ui::TreeRootCanvas(widget), ui::ViewportScale());
			} else {
				sorting::Tick(sorting::Mode::None, nullptr, nullptr, 1);
				if (search::Context() == menus::Menu::kNone) keyboard::Close("menu closed");
			}
		}
		hoverfix::Tick(menus::Active());
		search::Tick(menus::Active());
		PublishInputState();
	}
}
