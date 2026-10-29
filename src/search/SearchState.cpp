#include "search/SearchInternal.h"

#include "search/SearchConfig.h"
#include "engine/ContainerMenu.h"
#include "engine/PEHook.h"
#include "engine/QuestsMenu.h"
#include "engine/Reflect.h"
#include "engine/TextUtil.h"
#include "engine/Ui.h"
#include "engine/UiText.h"

namespace msearch::detail
{
	std::mutex        g_qMutex;
	std::string       g_queries[6];
	bool              g_selAll[6] = {};
	std::atomic<bool> g_typing{ false };
	std::atomic<int>  g_typingMenu{ 0 };
	std::atomic<bool> g_sideShown[5];
	double            g_canvasW = 1920;
	std::atomic<bool> g_external{ false };
	DWORD             g_gameThread = 0;
	BarWidgets        g_bar[6];
	PxRect            g_barPx[6];
	HWND              g_hwnd = nullptr;

	bool IsSide(menus::Menu a_menu) { return a_menu == menus::Menu::kContainerYours || a_menu == menus::Menu::kContainerTheirs; }
	bool MenuEnabled(menus::Menu a_menu)
	{

		return (a_menu == menus::Menu::kInventory && searchcfg::Get().inventory) || (a_menu == menus::Menu::kMagic && searchcfg::Get().magic)|| (IsSide(a_menu) && searchcfg::Get().container && g_sideShown[static_cast<int>(a_menu)].load()) || (a_menu == menus::Menu::kQuests && searchcfg::Get().quests);
	}
	bool OwnBarOn() { return (searchcfg::Get().inventory || searchcfg::Get().magic || searchcfg::Get().container || searchcfg::Get().quests); }
	// a menu is present when it is the open one; in the container / barter menu both sides are
	bool Present(menus::Menu a_menu, menus::Menu a_active)
	{
		return a_menu == a_active || (IsSide(a_menu) && a_active == menus::Menu::kNone && container::Open()) || (a_menu == menus::Menu::kQuests && a_active == menus::Menu::kNone && quests::Open());
	}
	UE::UObject* MenuWidget(menus::Menu a_menu) { return IsSide(a_menu) ? container::Widget() : a_menu == menus::Menu::kQuests ? quests::Page() : menus::Widget(a_menu); }
	std::string QueryOf(menus::Menu a_menu)
	{
		std::scoped_lock l(g_qMutex);
		return g_queries[static_cast<int>(a_menu) % 6];
	}
	bool SelAllOf(menus::Menu a_menu)
			{
				std::scoped_lock l(g_qMutex);
				return g_selAll[static_cast<int>(a_menu) % 6];
			}
			void SetQuery(menus::Menu a_menu, const std::string& a_text)
{
		std::scoped_lock l(g_qMutex);
		g_queries[static_cast<int>(a_menu) % 6] = a_text;
	}

	// a side of the container / barter menu is on the screen (a plain container shows only its own list)
	bool SideShownNow(menus::Menu a_side)
	{
		auto* content = container::SideContent(a_side);
		return content && ui::ShownOnScreen(content);
	}
}
