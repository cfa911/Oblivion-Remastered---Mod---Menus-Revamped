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

		struct Shown { UE::UObject* widget = nullptr; bool shown = false; };
		int   g_tickDepth = 0;
		Shown g_menuShown, g_sideShown[2];   // within the current TickScope

		bool ShownNow(Shown& a_memo, UE::UObject* a_widget)
		{
			if (!g_tickDepth) return ui::ShownOnScreen(a_widget);
			if (a_memo.widget != a_widget) a_memo = { a_widget, ui::ShownOnScreen(a_widget) };
			return a_memo.shown;
		}

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
		auto* widget = g_active ? reflect::Get(g_widget) : nullptr;
		return widget && ShownNow(g_menuShown, widget);
	}

	TickScope::TickScope()
	{
		if (!g_tickDepth++) {
			g_menuShown = {};
			g_sideShown[0] = g_sideShown[1] = {};
		}
	}

	TickScope::~TickScope() { --g_tickDepth; }

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

	bool SideShown(int side)
	{
		auto* content = SideContent(side);
		return content && ShownNow(g_sideShown[side == 3 ? 0 : 1], content);
	}

	void SetTransferHandlers(void (*a_started)(), void (*a_settled)())
	{
		g_transferStarted = a_started;
		g_transferSettled = a_settled;
	}
}
