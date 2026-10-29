#include "engine/ContainerMenu.h"
#include "engine/Menus.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "sorting/rules/TransferHold.h"
#include "engine/Ui.h"

namespace container
{
	namespace
	{
		constexpr auto kPath = L"/Game/UI/Modern/GameMenuLayer/Container_Inventory/WBP_ModernMenu_Container.WBP_ModernMenu_Container_C";
		UE::UClass* g_class = nullptr;
		reflect::Handle g_widget;
		bool g_active = false;
		struct CachedWidget
		{
			reflect::Handle owner, widget;
			std::string name;
			std::uint64_t nextLook = 0;
		};
		std::vector<CachedWidget> g_widgets;
		void (*g_transferStarted)() = nullptr;
		void (*g_transferSettled)() = nullptr;

		void OnEvent(UE::UObject* object, UE::UFunction* function, void*)
		{
			const auto name = pe::FunctionName(function);
			if (name == "BP_OnActivated") {
				g_widgets.clear();
				g_widget = reflect::Hold(object); g_active = true;
			} else if (name == "BP_OnDeactivated") {
				g_active = false; g_widgets.clear();
			} else if (transferhold::IsTransferAction(name)) {
				if (g_transferStarted) g_transferStarted();
			} else if (transferhold::IsSettled(name)) {
				if (g_transferSettled) g_transferSettled();
			}
		}
	}

	void Tick()
	{
		if (g_class || !reflect::Ok()) return;
		auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kPath);
		if (!cls || !pe::Watch(cls, &OnEvent)) return;
		g_class = cls;
		logger::info("Menus Revamped: MR-CONTAINER-CACHE-20261026-R2 native widget cache enabled");
		for (auto* object : reflect::Instances(cls)) {
			if (ui::ShownOnScreen(object)) { g_widget = reflect::Hold(object); g_active = true; break; }
		}
	}

	bool Open()
	{
		if (!menus::AnyOpen() || !reflect::Get(g_widget)) {
			g_active = false; g_widgets.clear();
		}
		return g_active && reflect::Get(g_widget) && ui::ShownOnScreen(reflect::Get(g_widget));
	}

	UE::UClass* Class() { return g_class; }
	UE::UObject* Widget() { return Open() ? reflect::Get(g_widget) : nullptr; }
	UE::UObject* FindWidget(UE::UObject* owner, std::string_view name)
	{
		if (!owner || !g_active) return nullptr;
		// Only stable native layout widgets are cached. Never retain raw child pointers
		// or rescan a missing widget on every ProcessEvent/controller read.
		std::erase_if(g_widgets, [](const auto& entry) { return !reflect::Get(entry.owner); });
		CachedWidget* cached = nullptr;
		for (auto& entry : g_widgets) {
			if (reflect::Get(entry.owner) == owner && entry.name == name) { cached = &entry; break; }
		}
		if (!cached) {
			g_widgets.push_back({ reflect::Hold(owner), {}, std::string(name), 0 });
			cached = &g_widgets.back();
		}
		if (auto* widget = reflect::Get(cached->widget)) return widget;
		if (cached->widget.ptr) { cached->widget = {}; cached->nextLook = 0; }
		const auto now = GetTickCount64();
		if (now < cached->nextLook) return nullptr;
		cached->nextLook = now + 500;
		auto* widget = ui::FindInTree(owner, name);
		cached->widget = reflect::Hold(widget);
		return widget;
	}
	UE::UObject* SideContent(int side)
	{
		auto* widget = Widget();
		return widget && (side == 3 || side == 4) ?
			FindWidget(widget, side == 3 ? "cont_give_maincontent" : "cont_take_maincontent") : nullptr;
	}

	void SetTransferHandlers(void (*a_started)(), void (*a_settled)())
	{
		g_transferStarted = a_started;
		g_transferSettled = a_settled;
	}
}
