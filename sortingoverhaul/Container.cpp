#include "Container.h"
#include "Menus.h"
#include "PEHook.h"
#include "Reflect.h"
#include "Ui.h"

namespace container
{
	namespace
	{
		constexpr auto kPath = L"/Game/UI/Modern/GameMenuLayer/Container_Inventory/WBP_ModernMenu_Container.WBP_ModernMenu_Container_C";
		UE::UClass* g_class = nullptr;
		reflect::Handle g_widget;
		bool g_active = false;

		void OnEvent(UE::UObject* object, UE::UFunction* function, void*)
		{
			const auto name = pe::FunctionName(function);
			if (name == "BP_OnActivated") { g_widget = reflect::Hold(object); g_active = true; }
			else if (name == "BP_OnDeactivated") g_active = false;
		}
	}

	void Tick()
	{
		if (g_class || !reflect::Ok()) return;
		auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kPath);
		if (!cls || !pe::Watch(cls, &OnEvent)) return;
		g_class = cls;
		for (auto* object : reflect::Instances(cls)) {
			if (ui::ShownOnScreen(object)) { g_widget = reflect::Hold(object); g_active = true; break; }
		}
	}

	bool Open()
	{
		if (!menus::AnyOpen()) g_active = false;
		return g_active && reflect::Get(g_widget) && ui::ShownOnScreen(reflect::Get(g_widget));
	}

	UE::UClass* Class() { return g_class; }
	UE::UObject* Widget() { return Open() ? reflect::Get(g_widget) : nullptr; }
	UE::UObject* SideContent(int side)
	{
		auto* widget = Widget();
		return widget && (side == 3 || side == 4) ?
			ui::FindInTree(widget, side == 3 ? "cont_give_maincontent" : "cont_take_maincontent") : nullptr;
	}
}
