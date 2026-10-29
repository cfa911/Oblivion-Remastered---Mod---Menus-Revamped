#include "search/SearchInternal.h"

#include "search/SearchConfig.h"
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
		constexpr float kBarFill[4] = { 0.16f, 0.09f, 0.04f, 1.0f }, kBarEdge[4] = { 0.06f, 0.035f, 0.018f, 1.0f }, kBarFocus[4] = { 0.78f, 0.62f, 0.28f, 1.0f };
		constexpr float kBarInk[4] = { 0.93f, 0.90f, 0.82f, 1.0f }, kBarHint[4] = { 0.58f, 0.53f, 0.45f, 1.0f };

		// the user widget (the menu) a widget sits in
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
		auto* wrapper = IsSide(a_menu) || a_menu == menus::Menu::kQuests ? nullptr : ui::FindInTree(menuW, a_menu == menus::Menu::kMagic ? "Magic_Spell_List" : "inv_cont_listview");
		auto* owner = wrapper ? OwnerWidget(wrapper) : nullptr;
		std::vector<UE::UObject*> cands{ menuW, owner };
		if (a_menu == menus::Menu::kQuests) {
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
		// the bar's panel: a PNG from the plugin's folder (black for the Inventory and the Magic menu, green for your side of the container / barter menu, red
		// for the other side), else the black one, else a plain rounded box
		UE::UObject* BarTexture(menus::Menu a_menu)
		{
			const int slot = a_menu == menus::Menu::kContainerYours ? 1 : a_menu == menus::Menu::kContainerTheirs ? 2 : 0;
			static reflect::Handle s_tex[3];
			static bool            s_failed[3] = { false, false, false };
			if (auto* t = reflect::Get(s_tex[slot])) return t;
			if (s_failed[slot]) return slot ? BarTexture(menus::Menu::kInventory) : nullptr;
			const wchar_t* file = slot == 1 ? L"SearchBarYours.png" : slot == 2 ? L"SearchBarTheirs.png" : L"SearchBar.png";
			const auto     path = mrconfig::PluginFolder() / L"MenusRevamped" / L"Search" / file;
			auto*          tex = ui::ImportTexture(path);
			if (!tex) {
				s_failed[slot] = true;
				logger::warn("own bar: the bar image could not be loaded ({}{}) - {}", path.string(), std::filesystem::exists(path) ? "" : ", missing", slot ? "the black one is used" : "a plain box is used");
				return slot ? BarTexture(menus::Menu::kInventory) : nullptr;
			}
			s_tex[slot] = reflect::Hold(tex);
			logger::debug("own bar: the bar image is loaded from {}", path.filename().string());
			return tex;
		}

		// the container / barter menu's bar: above the side's header (menu units, local to the menu's root canvas); the container's list alone on the
		// screen (the player's side not shown) has a place of its own
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
			a_h = p.h;
			a_w = p.w > 0 ? p.w : hw + 2 * std::abs(p.x);
			a_x = hx - cx + p.x;
			a_y = hy - cy - a_h - 6 + p.y;
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

		// the quests tab's bar sits where the Magic menu's does; what the canvas it went into and the quest panels measure is logged, to check that
		void LogQuestLayout(UE::UObject* a_canvas)
		{
			double x = 0, y = 0, w = 0, h = 0;
			if (ui::Measure(a_canvas, x, y, w, h)) logger::debug("own bar: the quests tab - the canvas the bar goes into is at {:.0f},{:.0f} {:.0f}x{:.0f}", x, y, w, h);
			if (auto* page = quests::Page(); page && ui::Measure(page, x, y, w, h)) logger::debug("own bar:   quests page at {:.0f},{:.0f} {:.0f}x{:.0f}", x, y, w, h);
			if (auto* panel = quests::ListPanel(); panel && ui::Measure(panel, x, y, w, h)) logger::debug("own bar:   quest list panel at {:.0f},{:.0f} {:.0f}x{:.0f}", x, y, w, h);
			if (auto* list = quests::ListWrapper(); list && ui::Measure(list, x, y, w, h)) logger::debug("own bar:   quest list at {:.0f},{:.0f} {:.0f}x{:.0f}", x, y, w, h);
		}

		bool BuildBar(menus::Menu a_menu, UE::UObject* a_canvas)
		{
			static int         s_n = 0;
			const std::wstring n = std::to_wstring(++s_n);
			const auto&        c = searchcfg::Get();
			const auto&        place = a_menu == menus::Menu::kMagic ? c.magicBar : a_menu == menus::Menu::kQuests ? c.questBar : c.inventoryBar;
			double             barX = place.x, barW = place.w, barH = place.h;
			double             barY = place.y;
			if (IsSide(a_menu) && !ContainerBarRect(a_menu, a_canvas, barX, barY, barW, barH, true)) return false;
			if (a_menu == menus::Menu::kQuests) LogQuestLayout(a_canvas);
			UE::UObject* slot = nullptr;
			auto*        frame = ui::AddToCanvas(a_canvas, L"/Script/UMG.Image", (L"IwmBarFrame" + n).c_str(), &slot);
			if (!frame || !slot) return false;
			ui::Vec2(slot, L"SetPosition", barX, barY);
			ui::Vec2(slot, L"SetSize", barW, barH);
			const std::int32_t z = 1000;
			ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			bool textured = false;
			if (auto* tex = BarTexture(a_menu)) {
				ui::Call b(frame, L"SetBrushFromTexture");
				b.Set("Texture", tex);
				b.Set("bMatchSize", false);
				textured = b.Run();
			}
			if (!textured) ui::RoundedBox(frame, false, kBarFill, kBarEdge, 2.0f);
			ui::Visible(frame, true);
			UE::UObject* lslot = nullptr;
			auto*        label = ui::AddToCanvas(a_canvas, L"/Script/UMG.TextBlock", (L"IwmBarText" + n).c_str(), &lslot);
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
			B.textured = textured;
			B.label = reflect::Hold(label);
			B.labelSlot = reflect::Hold(lslot);
			B.x = barX;
			B.y = barY;
			B.w = barW;
			B.h = barH;
			B.visible = true;
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
				g_selAll[static_cast<int>(a_menu) % 6] = false;
			}
			const std::string q = QueryOf(a_menu);
			const bool        caret = typing && (GetTickCount64() / 500) % 2 == 0;
			const bool        hint = q.empty() && !typing;
			const std::string show = hint ? B.hint : q + (typing ? (caret ? "|" : " ") : "");
			const bool        sel = typing && !q.empty() && SelAllOf(a_menu);   // selected: drawn in the bar's gold
			const std::string key = std::string(hint ? "#" : sel ? "*" : "=") + show;
			if (key != B.shown) {
				uix::SetText(label, show);
				uix::Ink(label, hint ? kBarHint : sel ? kBarFocus : kBarInk);
				uix::ApplyFont(label, searchcfg::Get().barFontSize, uix::NeedsFallbackFont(show));   // Greek, Cyrillic, kana, Han ...: a font that draws them
				B.shown = key;
			}
			if (typing != B.typing) {   // the bronze edge while it is being typed into
				if (!B.textured) ui::RoundedBox(frame, false, kBarFill, typing ? kBarFocus : kBarEdge, 2.0f);
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

		if (!MenuEnabled(a_menu)) {
			if (g_typingMenu.load() == static_cast<int>(a_menu)) g_typing = false;
			if (IsSide(a_menu)) HideBar(a_menu);
			return;
		}
		BarWidgets& B = g_bar[static_cast<int>(a_menu)];
		const bool  have = reflect::Get(B.frame) && reflect::Get(B.label);
		if (!have || a_now >= B.nextLook) {   // built, and looked at twice a second: it must still sit in the menu's canvas (a menu that was rebuilt or replaced does not take it along)
			B.nextLook = a_now + 500ms;
			auto* canvas = BarCanvasFor(a_menu, !have);
			if (!canvas) {
				if (!have) return;
			} else if (!have || canvas != reflect::Get(B.canvas)) {
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
		UpdateBar(a_menu, a_now);
	}
}
