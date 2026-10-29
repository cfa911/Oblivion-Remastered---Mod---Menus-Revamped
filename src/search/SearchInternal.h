#pragma once

// Shared between the search's files (SearchState, ItemNames, SearchBar, SearchInput, ListFilter, Search); not for other features.

#include "search/Search.h"
#include "search/ListLifecycle.h"
#include "engine/Reflect.h"

#include <mutex>

namespace msearch::detail
{
	using Clock = std::chrono::steady_clock;

	constexpr std::size_t kMaxText = 40;    // characters (code points) of a search text
	constexpr std::size_t kMaxBytes = 120;  // and bytes of its UTF-8: the controller keyboard reads it through a 128-byte buffer

	// the text of the search bar (one per menu, UTF-8), and the flags the window procedure shares with the game thread
	extern std::mutex        g_qMutex;   // the window procedure may run on another thread than the game's
	extern std::string       g_queries[6];
	extern bool              g_selAll[6];   // Ctrl+A: the whole text is selected - under g_qMutex
	extern std::atomic<bool> g_typing;
	extern std::atomic<int>  g_typingMenu;
	extern std::atomic<bool> g_sideShown[5];   // the container / barter menu's sides that are on the screen now
	extern double            g_canvasW;   // the container menu's root canvas, menu units
	extern std::atomic<bool> g_external;   // the controller keyboard has the text: typed keys are not taken
	extern DWORD             g_gameThread;

	bool         IsSide(menus::Menu a_menu);
	bool         MenuEnabled(menus::Menu a_menu);
	bool         OwnBarOn();
	bool         Present(menus::Menu a_menu, menus::Menu a_active);
	UE::UObject* MenuWidget(menus::Menu a_menu);
	std::string  QueryOf(menus::Menu a_menu);
	bool         SelAllOf(menus::Menu a_menu);
	void         SetQuery(menus::Menu a_menu, const std::string& a_text);
	bool         SideShownNow(menus::Menu a_side);

	// ---- the bars (SearchBar.cpp) ----
	struct BarWidgets
	{
		reflect::Handle   canvas, frame, edge, label, frameSlot, labelSlot;
		bool              textured = false;
		std::string       shown;
		std::string       hint = "Search";   // what the empty bar says (the container / barter menu's say whose list it is)
		bool              typing = false;
		bool              visible = true;
		double            x = 0, y = 0, w = 0, h = 0;   // where it sits in its canvas (menu units)
		Clock::time_point nextLook{}, nextMeasure{};
	};
	struct PxRect { double x = 0, y = 0, w = 0, h = 0; bool valid = false; };
	extern BarWidgets g_bar[6];
	extern PxRect     g_barPx[6];   // the bar's place in window pixels (g_qMutex)
	extern HWND       g_hwnd;

	UE::UObject* BarCanvasFor(menus::Menu a_menu, bool a_log = true);
	void         BarTick(menus::Menu a_menu, Clock::time_point a_now);

	// ---- names and matching (ItemNames.cpp) ----
	std::string ItemName(UE::UObject* a_item, menus::Menu a_menu, const char** a_route = nullptr);
	std::string Lower(const std::string& a_s);
	bool        Matches(UE::UObject* a_item, menus::Menu a_menu, const std::string& a_query);

	// ---- typing (SearchInput.cpp) ----
	bool AppendTyped(std::string& a_q, char32_t a_cp);

}

namespace msearch
{
	void FilterTick(menus::Menu menu, detail::Clock::time_point now);   // the list filter (ListFilter.cpp)
}
