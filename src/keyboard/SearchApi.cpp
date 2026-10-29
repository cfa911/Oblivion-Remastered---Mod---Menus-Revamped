#include "keyboard/SearchApi.h"
#include "search/Search.h"

namespace search
{
	bool Present() { return true; }
	bool Enabled(menus::Menu menu) { return msearch::Enabled(menu); }
	std::string GetText(menus::Menu menu) { return msearch::GetText(menu); }
	void SetText(menus::Menu menu, const std::string& text) { msearch::SetText(menu, text); }
	void SetExternalInput(bool open) { msearch::SetExternalInput(open); }
	bool PanelOnLeft(menus::Menu menu) { return msearch::PanelOnLeft(menu); }
	menus::Menu Context() { return static_cast<menus::Menu>(msearch::Context()); }
}
