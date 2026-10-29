#include "search/SearchInternal.h"

#include "search/SearchConfig.h"
#include "engine/ContainerMenu.h"
#include "engine/PEHook.h"
#include "engine/QuestsMenu.h"
#include "engine/Reflect.h"
#include "engine/TextUtil.h"
#include "engine/Ui.h"
#include "engine/UiText.h"

namespace search
{
	using namespace detail;

	void Tick(menus::Menu a_menu)
	{
		const auto now = Clock::now();
		const bool cont = a_menu == menus::Menu::kNone && container::Open();   // the container / barter menu: a bar and a list on each side
		{   // which of its sides are on the screen: a plain container shows only its own list (nothing to give)
			static auto nextShown = Clock::now();
			if (!cont) {
				g_sideShown[static_cast<int>(menus::Menu::kContainerYours)] = false;
				g_sideShown[static_cast<int>(menus::Menu::kContainerTheirs)] = false;
			} else if (now >= nextShown) {
				nextShown = now + 100ms;
				for (const auto side : { menus::Menu::kContainerYours, menus::Menu::kContainerTheirs }) {
					const bool shown = SideShownNow(side);
					if (shown != g_sideShown[static_cast<int>(side)].load()) logger::debug("container menu: the {} is {}", menus::Name(side), shown ? "on the screen" : "not on the screen");
					g_sideShown[static_cast<int>(side)] = shown;
				}
			}
		}
		const bool quest = !cont && a_menu == menus::Menu::kNone && quests::Open();   // the quests tab: a bar and the quest list
		if (g_typing.load()) {
			const auto typed = static_cast<menus::Menu>(g_typingMenu.load());
			if ((typed != a_menu && !(cont && IsSide(typed)) && !(quest && typed == menus::Menu::kQuests)) || g_external.load()) g_typing = false;
		}
		if (cont) {
			for (const auto side : { menus::Menu::kContainerYours, menus::Menu::kContainerTheirs }) {
				BarTick(side, now);
				FilterTick(side, now);
			}
		} else if (quest) {
			BarTick(menus::Menu::kQuests, now);
			FilterTick(menus::Menu::kQuests, now);
		} else {
			BarTick(a_menu, now);
			FilterTick(a_menu, now);
		}
	}

	void SetGameThread(DWORD a_id) { g_gameThread = a_id; }
	bool Enabled(menus::Menu a_menu) { return MenuEnabled(a_menu); }
	std::string GetText(menus::Menu a_menu) { return QueryOf(a_menu); }
	void SetText(menus::Menu a_menu, const std::string& a_text)
	{
		std::string s;   // a_text is UTF-8 (the keyboard plugin sends plain ASCII); what cannot be typed is left out
		textutil::ForEachCodePoint(textutil::Wide(a_text), [&](char32_t a_cp) { AppendTyped(s, a_cp); });
		SetQuery(a_menu, s);
	}
	bool PanelOnLeft(menus::Menu a_menu)
	{
		const auto& B = g_bar[static_cast<int>(a_menu) % 6];
		if (IsSide(a_menu) && B.w > 0) return B.x + B.w * 0.5 < g_canvasW * 0.5;
		return a_menu != menus::Menu::kContainerTheirs;
	}

	menus::Menu Context()
	{
		const auto m = menus::Active();
		if (m != menus::Menu::kNone) return m;
		if (searchcfg::Get().container && container::Open()) return menus::Menu::kContainerYours;
		return searchcfg::Get().quests && quests::Open() ? menus::Menu::kQuests : menus::Menu::kNone;
	}
}
