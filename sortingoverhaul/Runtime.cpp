// Independent UI/input/list lifecycle for Sorting Overhaul. SPDX-License-Identifier: GPL-3.0-or-later
#include "Runtime.h"
#include "GearFilter.h"
#include "Menus.h"
#include "PEHook.h"
#include "Reflect.h"
#include "Ui.h"

namespace sorting
{
	namespace
	{
		struct Context { reflect::Handle menu, canvas, list; bool watched = false; };
		Context g_contexts[3];
		WNDPROC g_previous = nullptr;
		DWORD g_thread = 0;
		bool g_inside = false;
		std::atomic<bool> g_returnToListAfterFilter{ true };
		constexpr UINT_PTR kTimer = 0x534F;
		using Match = bool (*)(UE::UObject*, const char*);
		Match g_match = nullptr;
		bool MatchName(UE::UObject* item, const std::string& query) { return !g_match || g_match(item, query.c_str()); }

		bool SearchDrives(int menu)
		{
			using Drives = bool (*)(int);
			auto module = GetModuleHandleW(L"MenuSearch.dll");
			auto drives = module ? reinterpret_cast<Drives>(GetProcAddress(module, "MenuSearch_DrivesSorting")) : nullptr;
			return drives && drives(menu);
		}

		int Count(UE::UObject* list)
		{
			ui::Call count(list, L"GetNumItems");
			return count && count.Run() ? count.Get<int>("ReturnValue") : -1;
		}

		bool Items(UE::UObject* list, std::vector<UE::UObject*>& items)
		{
			ui::Call get(list, L"GetListItems");
			auto* result = get.At("ReturnValue");
			if (!result || !get.Run()) return false;
			auto** data = *reinterpret_cast<UE::UObject***>(result);
			const int count = *reinterpret_cast<int*>(static_cast<std::uint8_t*>(result) + 8);
			const bool valid = count >= 0 && count <= 4000 && (count == 0 || data);
			if (valid) for (int i = 0; i < count; ++i) if (data[i]) items.push_back(data[i]);
			if (data) UE::FMemory::Free(data);
			return valid;
		}

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

		void AfterMenu(UE::UObject*, UE::UFunction* function, void*)
		{
			if (g_inside) return;
			const auto name = pe::FunctionName(function);
			if (name == "UpdateItemList" || name == "ChangeTab" || name == "BP_OnActivated" || name == "UpdateCurrentPage" || name == "UpdateCurrentPageInventory") {
				gearfilter::Invalidate();
				Tick();
			}
		}

		UE::UObject* Prepare(int menu)
		{
			if (menu != 1 && menu != 2) return nullptr;
			if (!reflect::Ok() && !reflect::SelfCheck()) return nullptr;
			const auto type = static_cast<menus::Menu>(menu);
			auto* widget = menus::Widget(type);
			if (!widget) return nullptr;
			auto& context = g_contexts[menu];
			if (reflect::Get(context.menu) != widget) {
				context.menu = reflect::Hold(widget); context.canvas = {}; context.list = {};
			}
			if (!context.watched) if (auto* cls = menus::ClassOf(type)) context.watched = pe::WatchPost(cls, &AfterMenu);
			if (!reflect::Get(context.canvas)) if (auto* canvas = RootCanvas(widget)) context.canvas = reflect::Hold(canvas);
			if (!reflect::Get(context.list)) {
				auto* list = ui::FindInTree(widget, menu == 2 ? "Magic_Spell_List" : "inv_cont_listview");
				if (list && Count(list) < 0) {
					ui::Call inner(list, L"GetListView");
					list = inner && inner.Run() ? inner.Get<UE::UObject*>("ReturnValue") : nullptr;
				}
				if (list && Count(list) >= 0) context.list = reflect::Hold(list);
			}
			gearfilter::Tick(menu == 2 ? gearfilter::Mode::Magic : gearfilter::Mode::Inventory, widget, reflect::Get(context.canvas), Scale());
			return reflect::Get(context.list);
		}

		LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM key, LPARAM position)
		{
			if (message == WM_TIMER && key == kTimer && GetCurrentThreadId() == g_thread) { Tick(); return 0; }
			if (gearfilter::Message(message, key, position)) return 0;
			return CallWindowProcW(g_previous, window, message, key, position);
		}

		BOOL CALLBACK FindWindow(HWND window, LPARAM result)
		{
			DWORD process = 0; GetWindowThreadProcessId(window, &process);
			wchar_t name[64]{}; GetClassNameW(window, name, 64);
			if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) || std::wcscmp(name, L"UnrealWindow")) return TRUE;
			*reinterpret_cast<HWND*>(result) = window; return FALSE;
		}

		void WatchWindow()
		{
			static ULONGLONG next = 0;
			if (g_previous || GetTickCount64() < next) return;
			next = GetTickCount64() + 1000;
			HWND window = nullptr; EnumWindows(&FindWindow, reinterpret_cast<LPARAM>(&window));
			if (!window) return;
			g_previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc)));
			if (g_previous && GetWindowThreadProcessId(window, nullptr) == g_thread) SetTimer(window, kTimer, 8, nullptr);
		}
	}

	void LoadSettings()
	{
		HMODULE module = nullptr;
		const auto flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
		if (!GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&g_returnToListAfterFilter), &module)) return;
		std::array<wchar_t, 32768> filename{};
		const auto size = GetModuleFileNameW(module, filename.data(), static_cast<DWORD>(filename.size()));
		if (!size || size >= filename.size()) return;
		auto ini = std::filesystem::path(filename.data());
		ini.replace_extension(L".ini");
		g_returnToListAfterFilter.store(GetPrivateProfileIntW(L"Controller", L"bReturnToListAfterFilter", 1, ini.c_str()) != 0);
	}

	bool ReturnToListAfterFilter() { return g_returnToListAfterFilter.load(); }

	bool FilterList(int menu, UE::UObject* list, const char* query, Match matches)
	{
		if (g_inside || (menu != 1 && menu != 2) || !list) return false;
		g_inside = true;
		struct Exit { ~Exit() { g_match = nullptr; g_inside = false; } } exit;
		menus::Tick();
		if (Prepare(menu) != list || !gearfilter::Active()) return false; // Active effects retains SearchLight's ordinary text search.
		std::vector<UE::UObject*> items;
		if (!Items(list, items)) return true; // Never partially rewrite an unreadable/oversized list.
		g_match = matches;
		gearfilter::Filter(list, items, query ? query : "", &MatchName);
		return true;
	}

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
			if (reflect::Ok()) menus::Tick();
		}
		if (!reflect::Ok()) return;
		const int menu = static_cast<int>(menus::Active());
		if (menu != 1 && menu != 2) { gearfilter::Tick(gearfilter::Mode::None, nullptr, nullptr, 1); return; }
		auto* list = Prepare(menu);
		if (!list || !gearfilter::Active() || SearchDrives(menu)) return;
		std::vector<UE::UObject*> items;
		if (Items(list, items)) gearfilter::Filter(list, items, "", &MatchName);
	}
}
