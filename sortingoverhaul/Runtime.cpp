// Menus Revamped: single discovery, window-message and UI lifecycle.
#include "Runtime.h"
#include "GearFilter.h"
#include "Container.h"
#include "Menus.h"
#include "Reflect.h"
#include "Ui.h"
#include "../revamped/Config.h"
#include "../menusearch/MenuSearch.h"
#include "../menusearch/Quests.h"
#include "../searchkb/SearchKeyboard.h"

namespace sorting
{
	namespace
	{
		WNDPROC g_previous = nullptr;
		DWORD g_thread = 0;
		bool g_inside = false;
		std::atomic<bool> g_keyboardOpen{ false };
		constexpr UINT_PTR kTimer = 0x4D52;

		UE::UObject* RootCanvas(UE::UObject* widget)
		{
			auto** tree = widget ? reflect::At<UE::UObject*>(widget, reflect::Offset(widget->GetClass(), "WidgetTree")) : nullptr;
			auto** root = tree && *tree ? reflect::At<UE::UObject*>(*tree, reflect::Offset((*tree)->GetClass(), "RootWidget")) : nullptr;
			auto* canvas = ui::Class(L"/Script/UMG.CanvasPanel");
			return root && *root && canvas && (*root)->GetClass()->IsChildOf(canvas) ? *root : nullptr;
		}

		float Scale()
		{
			auto* lib = ui::Class(L"/Script/UMG.WidgetLayoutLibrary");
			ui::Call scale(lib ? lib->GetDefaultObject(false) : nullptr, L"GetViewportScale");
			scale.Set("WorldContextObject", ui::PlayerController());
			return scale && scale.RunGuarded() ? scale.Get<float>("ReturnValue") : 1.0f;
		}

		LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM key, LPARAM position)
		{
			if (message == WM_TIMER && key == kTimer && GetCurrentThreadId() == g_thread) { Tick(); return 0; }
			if (gearfilter::PopupOpen() && gearfilter::Message(message, key, position)) return 0;
			LRESULT result = 0;
			if (msearch::HandleMessage(window, message, key, position, result)) return result;
			if (gearfilter::Message(message, key, position)) return 0;
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
			msearch::SetWindow(window);
			if (GetWindowThreadProcessId(window, nullptr) == g_thread) SetTimer(window, kTimer, 8, nullptr);
		}
	}

	bool ReturnToListAfterFilter() { return mrconfig::Get().returnToListAfterFilter; }
	bool ControllerKeyboardOpen() { return g_keyboardOpen.load(); }
	void PublishInputState() { g_keyboardOpen.store(osk::Taking()); }

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
			gearfilter::Tick(gearfilter::Mode::Container, container::SideContent(3),
				RootCanvas(container::Widget()), Scale(), container::SideContent(4));
		} else {
			const auto menu = menus::Active();
			if (menu == menus::Menu::kInventory || menu == menus::Menu::kMagic) {
				auto* widget = menus::Widget(menu);
				gearfilter::Tick(menu == menus::Menu::kMagic ? gearfilter::Mode::Magic : gearfilter::Mode::Inventory,
					widget, RootCanvas(widget), Scale());
			} else {
				gearfilter::Tick(gearfilter::Mode::None, nullptr, nullptr, 1);
				if (msearch::Context() == 0) osk::Close("menu closed");
			}
		}
		msearch::Tick(menus::Active());
		PublishInputState();
	}
}
