#include "search/SearchInternal.h"

#include "search/SearchConfig.h"
#include "alchemy/AlchemyMenu.h"
#include "core/Config.h"
#include "engine/ContainerMenu.h"
#include "engine/PEHook.h"
#include "engine/QuestsMenu.h"
#include "engine/Reflect.h"
#include "engine/TextUtil.h"
#include "engine/Ui.h"
#include "engine/UiText.h"

namespace search::detail
{
	namespace
	{
		// ---- our own search bar (Inventory and Magic) ----
		// A bar in the menu's own root canvas, where the search mods put theirs, and the typing for it: while it is being typed into, the
		// game window's keys are taken here (the menu's hotkeys never see them), Ctrl+F / Ctrl+K or a click on the bar starts typing, Enter or
		// Esc ends it. The text lives in g_queries; the filter and the controller's keyboard read and write the same text.
		const uistyle::Search& BarStyle(menus::Menu a_menu)
		{
			// the skinned menus (Inventory, Magic) take the menu skin's bar in Dark (phase 2, owner 2026-11-10); Alchemy and its
			// Filters panel too since their parchments are dark (owner 2026-11-11); the container/barter sides like the Inventory
			// (Step C.1, owner 2026-11-12: "use the styling of the inv search bar, dark. It is brown rn")
			const auto preset = mrconfig::Get().stylePreset;
			const auto& base = uistyle::SearchFor(a_menu == menus::Menu::kInventory || a_menu == menus::Menu::kMagic ||
				a_menu == menus::Menu::kQuests || a_menu == menus::Menu::kAlchemy || a_menu == menus::Menu::kAlchemyFilters ||
				a_menu == menus::Menu::kSpellmaking ||
				a_menu == menus::Menu::kContainerYours || a_menu == menus::Menu::kContainerTheirs, preset);
			// Custom (C.3 round 2, owner 2026-11-14): the Filters "Search effects" box like the filter chips: the run-time box fill
			// and outline (Pack Builder Runtime tab); the ingredient bar keeps the Inventory look
			if (a_menu != menus::Menu::kAlchemyFilters || !uistyle::Recolors(preset)) return base;
			static uistyle::Search s_filters{};
			const auto& skin = darkstyle::Get().skin;
			s_filters = base;
			s_filters.fill = skin.alchemyBox;
			s_filters.edge = skin.alchemyBoxLine;
			return s_filters;
		}

		const float* BarTextInk(menus::Menu a_menu, bool hint, bool sel)
		{
			const auto& style = BarStyle(a_menu);
			if ((a_menu == menus::Menu::kAlchemy || a_menu == menus::Menu::kAlchemyFilters) && !uistyle::SkinFor(mrconfig::Get().stylePreset).recolor)
				return style.alchemyInk.data();
			return hint ? style.hint.data() : sel ? style.selectedInk.data() : style.ink.data();
		}

		UE::UObject* OwnerWidget(UE::UObject* a_o)
		{
			static auto* userWidget = ui::Class(L"/Script/UMG.UserWidget");
			for (auto* p = a_o ? a_o->GetOuter() : nullptr; p && userWidget; p = p->GetOuter()) {
				if (p->GetClass() && p->GetClass()->IsChildOf(userWidget)) return p;
			}
			return nullptr;
		}

	}

	// the root canvas the bar goes into: that of the user widget that holds the list (the Inventory's search bar is in a widget inside the
	// menu), else the menu's own
	UE::UObject* BarCanvasFor(menus::Menu a_menu, bool a_log)
	{
		auto* menuW = MenuWidget(a_menu);
		if (!menuW || !reflect::Ok()) return nullptr;
		auto* wrapper = IsSide(a_menu) || a_menu == menus::Menu::kQuests || a_menu == menus::Menu::kWorldMap || a_menu == menus::Menu::kSpellmaking ? nullptr : ui::FindInTree(menuW, a_menu == menus::Menu::kMagic ? "Magic_Spell_List" : "inv_cont_listview");
		auto* owner = wrapper ? OwnerWidget(wrapper) : nullptr;
		std::vector<UE::UObject*> cands{ menuW, owner };
		if (a_menu == menus::Menu::kQuests || a_menu == menus::Menu::kWorldMap || a_menu == menus::Menu::kAlchemyFilters) {
			for (auto* p = OwnerWidget(menuW); p; p = OwnerWidget(p)) cands.push_back(p);
		}
		for (auto* w : cands) {
			if (auto* canvas = ui::TreeRootCanvas(w)) {
				if (a_log) logger::debug("own bar: the {}'s bar goes into the root canvas of {}", menus::Name(a_menu), pe::Utf8(w->GetClass()->GetFName().ToString()));
				return canvas;
			}
		}
		return nullptr;
	}

	namespace
	{
		// the container / barter menu's bar (menu units, local to the menu's root canvas). Step C.1 (owner on A0A6D6BA: "the search bar is not in
		// the same position as in the inv"; accepted on C8FB448F, "the bars are correctly positioned"): like the Inventory and Magic bars it
		// sits in the side's title row (centred on inv_cont_title), right of the tab name, its right edge kSideBarRightInset inside the side's
		// header (Magic: list edge 904.3, bar end 900), with the Inventory bar's size. The [ContainerYours/Theirs/Take] title-row keys offset it.
		constexpr double kSideBarRightInset = 4;
		constexpr double kSideBarOwnerShift = 10;   // owner 2026-11-12 on E5F0F6DA: "shift both bars to the right ... 10 px shuld be enough"
		bool ContainerBarRect(menus::Menu a_side, UE::UObject* a_canvas, double& a_x, double& a_y, double& a_w, double& a_h, bool a_log)
		{
			auto* content = container::SideContent(a_side);
			auto* header = content ? container::FindWidget(content, "inv_cont_header") : nullptr;
			double hx = 0, hy = 0, hw = 0, hh = 0, cx = 0, cy = 0, cw = 0, ch = 0;
			if (!header || !ui::Measure(header, hx, hy, hw, hh) || !ui::Measure(a_canvas, cx, cy, cw, ch) || hw < 10 || hh < 1) return false;
			const auto& c = searchcfg::Get();
			// the container's list without the tab row above it (a plain container, nothing to give) has a place of its own; with the tabs (beside yours in a
			// transfer or a barter, or beside an item's details) it sits like the barter's
			auto*       menuW = container::Widget();
			auto*       tabs = menuW ? container::FindWidget(menuW, a_side == menus::Menu::kContainerYours ? "cont_give_tabs" : "cont_take_tabs") : nullptr;
			const bool  take = a_side == menus::Menu::kContainerTheirs && !(tabs && ui::ShownOnScreen(tabs));
			const auto& p = take ? c.takeBar : a_side == menus::Menu::kContainerYours ? c.yoursBar : c.theirsBar;
			double tx = hx, ty = hy, tw = hw, th = hh;   // the title row; the header box if the title cannot be measured
			if (auto* title = container::FindWidget(content, "inv_cont_title"); !title || !ui::Measure(title, tx, ty, tw, th) || th < 1) { ty = hy; th = hh; }
			a_w = c.inventoryBar.w;
			a_h = c.inventoryBar.h;
			a_x = hx + hw - kSideBarRightInset - a_w + kSideBarOwnerShift - cx + p.x;
			a_y = ty + th * 0.5 - a_h * 0.5 - cy + p.y;
			if (cw > 100) g_canvasW = cw;
			if (a_log) {
				logger::debug("own bar: {}{} - header at {:.0f},{:.0f} {:.0f}x{:.0f}, canvas at {:.0f},{:.0f} {:.0f}x{:.0f} -> bar at {:.0f},{:.0f} {:.0f}x{:.0f}", menus::Name(a_side), take ? " (its list alone)" : "", hx, hy, hw, hh, cx, cy, cw, ch, a_x, a_y, a_w, a_h);
				for (const char* name : { "inv_cont_title", "inv_cont_filter", "inv_cont_listview_overlay" }) {
					double x = 0, y = 0, w = 0, h = 0;
					if (auto* o = ui::FindInTree(content, name); o && ui::Measure(o, x, y, w, h)) logger::debug("own bar:   {} at {:.0f},{:.0f} {:.0f}x{:.0f}", name, x, y, w, h);
				}
			}
			return true;
		}

		// the Quests search bar sits in the list title row; log its canvas and the panel measures for layout checks
		void LogQuestLayout(UE::UObject* a_canvas)
		{
			double x = 0, y = 0, w = 0, h = 0;
			if (ui::Measure(a_canvas, x, y, w, h)) logger::debug("own bar: the quests tab - the canvas the bar goes into is at {:.0f},{:.0f} {:.0f}x{:.0f}", x, y, w, h);
			if (auto* page = quests::Page(); page && ui::Measure(page, x, y, w, h)) logger::debug("own bar:   quests page at {:.0f},{:.0f} {:.0f}x{:.0f}", x, y, w, h);
			if (auto* panel = quests::ListPanel(); panel && ui::Measure(panel, x, y, w, h)) logger::debug("own bar:   quest list panel at {:.0f},{:.0f} {:.0f}x{:.0f}", x, y, w, h);
			if (auto* list = quests::ListWrapper(); list && ui::Measure(list, x, y, w, h)) logger::debug("own bar:   quest list at {:.0f},{:.0f} {:.0f}x{:.0f}", x, y, w, h);
		}

		UE::UObject* BarPart(UE::UObject* a_canvas, const wchar_t* a_class, const std::wstring& a_name, UE::UObject** a_slot)
		{
			if (auto* found = ui::FindInTree(a_canvas, textutil::Utf8(a_name)); found && ui::IsWithin(found, a_canvas)) {
				const auto offset = reflect::Offset(found->GetClass(), "Slot");
				*a_slot = offset >= 0 ? *reflect::At<UE::UObject*>(found, offset) : nullptr;
				if (*a_slot) return found;
			}
			return ui::AddToCanvas(a_canvas, a_class, a_name.c_str(), a_slot);
		}

		bool SpellmakingBarRect(UE::UObject* a_canvas, double& x, double& y, double& w, double& h)
		{
			auto* menu = MenuWidget(menus::Menu::kSpellmaking);
			auto* title = menu ? ui::FindInTree(menu, "SpellMaking_TabName") : nullptr;
			auto* sort = menu ? ui::FindInTree(menu, "SpellMaking_Filters") : nullptr;
			double tx{}, ty{}, tw{}, th{}, sx{}, sy{}, sw{}, sh{}, cx{}, cy{}, cw{}, ch{};
			if (!title || !sort || !ui::Measure(title, tx, ty, tw, th) || !ui::Measure(sort, sx, sy, sw, sh) ||
				!ui::Measure(a_canvas, cx, cy, cw, ch)) return false;

			constexpr double gap = 10;
			x = tx + tw + gap - cx;
			w = sx - cx - gap - x;
			h = std::min(th, 36.0);
			y = ty + (th - h) * 0.5 - cy;
			return w >= 50 && h >= 10;
		}

		bool BuildBar(menus::Menu a_menu, UE::UObject* a_canvas)
		{
			static int         s_n = 0;
			const std::wstring n = a_menu == menus::Menu::kAlchemyFilters ? L"AlchemyFilters" : a_menu == menus::Menu::kAlchemy ? L"Alchemy" : std::to_wstring(++s_n);
			const auto&        c = searchcfg::Get();
			const auto&        place = a_menu == menus::Menu::kWorldMap ? c.mapBar : a_menu == menus::Menu::kMagic ? c.magicBar : a_menu == menus::Menu::kQuests ? c.questBar : c.inventoryBar;
			double             barX = place.x, barW = place.w, barH = place.h;
			double             barY = place.y;
			if (IsSide(a_menu) && !ContainerBarRect(a_menu, a_canvas, barX, barY, barW, barH, true)) return false;
			if ((a_menu == menus::Menu::kAlchemy || a_menu == menus::Menu::kAlchemyFilters) && !alchemy::SearchRect(a_canvas, barX, barY, barW, barH, a_menu == menus::Menu::kAlchemyFilters)) return false;
			if (a_menu == menus::Menu::kSpellmaking && !SpellmakingBarRect(a_canvas, barX, barY, barW, barH)) return false;
			if (a_menu == menus::Menu::kQuests) LogQuestLayout(a_canvas);
			UE::UObject* slot = nullptr;
			auto*        frame = BarPart(a_canvas, L"/Script/UMG.Image", L"IwmBarFrame" + n, &slot);
			if (!frame || !slot) return false;
			ui::Vec2(slot, L"SetPosition", barX, barY);
			ui::Vec2(slot, L"SetSize", barW, barH);
			const std::int32_t z = 1000;
			ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			// the built-in box in every preset (owner 2026-11-17: Classic takes the former Light bar; the SearchBar PNGs are gone)
			const auto& style = BarStyle(a_menu);
			ui::RoundedBox(frame, false, style.fill.data(), style.edge.data(), 2.0f);
			ui::Visible(frame, true);
			UE::UObject* lslot = nullptr;
			auto*        label = BarPart(a_canvas, L"/Script/UMG.TextBlock", L"IwmBarText" + n, &lslot);
			if (!label || !lslot) return false;
			ui::Vec2(lslot, L"SetAlignment", 0.5, 0.5);
			const bool yes = true;
			ui::CallFirst(lslot, L"SetAutoSize", &yes, sizeof(yes));
			ui::Vec2(lslot, L"SetPosition", barX + barW * 0.5, barY + barH * 0.5);
			const std::int32_t z2 = 1001;
			ui::CallFirst(lslot, L"SetZOrder", &z2, sizeof(z2));
			ui::Visible(label, true);
			BarWidgets& B = g_bar[static_cast<int>(a_menu)];
			B.canvas = reflect::Hold(a_canvas);
			B.frame = reflect::Hold(frame);
			B.frameSlot = reflect::Hold(slot);
			B.label = reflect::Hold(label);
			B.labelSlot = reflect::Hold(lslot);
			B.x = barX;
			B.y = barY;
			B.w = barW;
			B.h = barH;
			B.visible = true;
			if (a_menu == menus::Menu::kAlchemy) B.hint = "Search ingredients or effects";
			if (a_menu == menus::Menu::kAlchemyFilters || a_menu == menus::Menu::kSpellmaking) B.hint = "Search effects";
			B.shown = "\x01";   // matches nothing real: the first update draws it
			B.typing = false;
			logger::debug("own bar: built in the {}", menus::Name(a_menu));
			return true;
		}

		// a side that is not on the screen has no bar
		void HideBar(menus::Menu a_menu)
		{
			BarWidgets& B = g_bar[static_cast<int>(a_menu)];
			if (!B.visible) return;
			if (auto* f = reflect::Get(B.frame)) ui::Visible(f, false);
			if (auto* l = reflect::Get(B.label)) ui::Visible(l, false);
			B.visible = false;
			g_barPx[static_cast<int>(a_menu)].valid = false;
		}

		void UpdateBar(menus::Menu a_menu, Clock::time_point a_now)
		{
			BarWidgets& B = g_bar[static_cast<int>(a_menu)];
			auto*       label = reflect::Get(B.label);
			auto*       frame = reflect::Get(B.frame);
			if (!label || !frame) return;
			if (!B.visible) {
				ui::Visible(frame, true);
				ui::Visible(label, true);
				B.visible = true;
			}
			const bool        typing = g_typing.load() && g_typingMenu.load() == static_cast<int>(a_menu);
			if (!typing) {
				std::scoped_lock l(g_qMutex);
				g_selAll[static_cast<int>(a_menu)] = false;
			}
			const std::string q = QueryOf(a_menu);
			const bool        caret = typing && (GetTickCount64() / 500) % 2 == 0;
			const bool        hint = q.empty() && !typing;
			const std::string show = hint ? B.hint : q + (typing ? (caret ? "|" : " ") : "");
			const bool        sel = typing && !q.empty() && SelAllOf(a_menu);   // selected: drawn in the bar's gold
			const std::string key = std::string(hint ? "#" : sel ? "*" : "=") + show;
			if (key != B.shown) {
				uix::SetText(label, show);
				uix::Ink(label, BarTextInk(a_menu, hint, sel));
				uix::ApplyFont(label, searchcfg::Get().barFontSize, uix::NeedsFallbackFont(show));   // Greek, Cyrillic, kana, Han ...: a font that draws them
				B.shown = key;
			}
			if (typing != B.typing) {   // the bronze edge while it is being typed into
				const auto& style = BarStyle(a_menu);
				ui::RoundedBox(frame, false, style.fill.data(), (typing ? style.focus : style.edge).data(), 2.0f);
				B.typing = typing;
			}
			if (a_now >= B.nextMeasure) {   // where it is in window pixels, for a click on it
				B.nextMeasure = a_now + 150ms;
				if (IsSide(a_menu)) {
					// yours says Player; the other side Seller in a barter (the merchant's gold row is on screen), else Container
					std::string want = "Player";
					if (a_menu == menus::Menu::kContainerTheirs) {
						auto* money = container::FindWidget(container::Widget(), "cont_take_money_verticalbox");
						want = money && ui::ShownOnScreen(money) ? "Seller" : "Container";
					}
					if (want != B.hint) {
						B.hint = want;
						logger::debug("own bar: the {}'s empty bar says {}", menus::Name(a_menu), want);
					}
					// the layout can change (the container's list alone, or beside yours): the bar follows its header
					auto*  canvas = reflect::Get(B.canvas);
					auto*  slot = reflect::Get(B.frameSlot);
					auto*  lslot = reflect::Get(B.labelSlot);
					double x = 0, y = 0, w = 0, h = 0;
					if (canvas && slot && lslot && ContainerBarRect(a_menu, canvas, x, y, w, h, false) &&
						(std::abs(x - B.x) > 0.5 || std::abs(y - B.y) > 0.5 || std::abs(w - B.w) > 0.5 || std::abs(h - B.h) > 0.5)) {
						ui::Vec2(slot, L"SetPosition", x, y);
						ui::Vec2(slot, L"SetSize", w, h);
						ui::Vec2(lslot, L"SetPosition", x + w * 0.5, y + h * 0.5);
						B.x = x;
						B.y = y;
						B.w = w;
						B.h = h;
						logger::debug("own bar: the {}'s bar moved to {:.0f},{:.0f} {:.0f}x{:.0f}", menus::Name(a_menu), x, y, w, h);
					}
				}
				double x = 0, y = 0, w = 0, h = 0;
				if (ui::Measure(frame, x, y, w, h)) {
					const double s = ui::ViewportScale();
					std::scoped_lock l(g_qMutex);
					g_barPx[static_cast<int>(a_menu)] = { x * s, y * s, w * s, h * s, true };
				}
			}
		}
	}

	void BarTick(menus::Menu a_menu, Clock::time_point a_now)
	{
		if (!OwnBarOn() || !reflect::Ok()) return;

		// RMI-039: the ingredient bar stays drawn while LB/RB focus chips or sort icons; typing into it stays blocked there
		const bool drawn = a_menu == menus::Menu::kAlchemy ? searchcfg::Get().alchemy && alchemy::SearchShown() :
			a_menu == menus::Menu::kQuests ? g_questSearchShown.load() : MenuEnabled(a_menu);
		if (!drawn || ((a_menu == menus::Menu::kAlchemy || a_menu == menus::Menu::kAlchemyFilters) && !alchemy::Open())) {
			if (g_typingMenu.load() == static_cast<int>(a_menu)) g_typing = false;
			if (IsSide(a_menu) || a_menu == menus::Menu::kQuests || a_menu == menus::Menu::kAlchemy ||
				a_menu == menus::Menu::kAlchemyFilters || a_menu == menus::Menu::kSpellmaking) {
				HideBar(a_menu);
				std::scoped_lock lock(g_qMutex);
				g_barPx[static_cast<int>(a_menu)] = {};
			}
			return;
		}
		BarWidgets& B = g_bar[static_cast<int>(a_menu)];
		const bool  have = reflect::Get(B.frame) && reflect::Get(B.label);
		const bool remeasure = a_now >= B.nextLook;
		if (!have || remeasure) {   // built, and looked at twice a second: it must still sit in the menu's canvas (a menu that was rebuilt or replaced does not take it along)
			B.nextLook = a_now + 500ms;
			auto* canvas = BarCanvasFor(a_menu, !have);
			if (!canvas) {
				if (!have) return;
			} else if (!have || canvas != reflect::Get(B.canvas) || !ui::IsWithin(reflect::Get(B.frame), canvas)) {
				if (have) logger::debug("own bar: the {}'s canvas is another one now - the bar is built again", menus::Name(a_menu));
				for (auto* w : { reflect::Get(B.frame), reflect::Get(B.edge), reflect::Get(B.label) }) {
					if (w) {
						ui::Call rm(w, L"RemoveFromParent");
						rm.Run();
					}
				}
				if (!BuildBar(a_menu, canvas)) return;
			}
		}
		const bool alchemyBar = a_menu == menus::Menu::kAlchemy || a_menu == menus::Menu::kAlchemyFilters;
		if (alchemyBar || (remeasure && a_menu == menus::Menu::kSpellmaking)) {
			auto* canvas = reflect::Get(B.canvas);
			double x = 0, y = 0, w = 0, h = 0;
			if (canvas && (alchemyBar ? alchemy::SearchRect(canvas, x, y, w, h, a_menu == menus::Menu::kAlchemyFilters) :
				SpellmakingBarRect(canvas, x, y, w, h))) {
				if (auto* slot = reflect::Get(B.frameSlot)) { ui::Vec2(slot, L"SetPosition", x, y); ui::Vec2(slot, L"SetSize", w, h); }
				if (auto* slot = reflect::Get(B.labelSlot)) ui::Vec2(slot, L"SetPosition", x + w * 0.5, y + h * 0.5);
				B.x = x; B.y = y; B.w = w; B.h = h;
			}
		}
		UpdateBar(a_menu, a_now);
	}
}
