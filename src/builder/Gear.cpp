// In-game Pack Builder Phase 1: the gear button that opens the builder window (owner decision 2026-11-21: mouse only,
// top-right corner of the player menu's item panel, preview v2 approved). SPDX-License-Identifier: GPL-3.0-or-later
// An own viewport-level UserWidget with a canvas holds one Image (the ScrollHints pattern), shown while the player menu
// WBP_ModernMenu_Player_C is on screen. The Image is HitTestInvisible: the click is hit-tested in the window procedure
// against ingame::GearRect (OverlayRules.h) with the viewport scale published here. Game thread only, except the atomics.
#include "builder/Builder.h"
#include "builder/OverlayRules.h"

#include "core/Config.h"
#include "engine/Menus.h"
#include "engine/Reflect.h"
#include "engine/Ui.h"

namespace builder::gear
{
	namespace
	{
		constexpr const wchar_t* kPlayerClass = L"/Game/UI/Legacy/GameMenuLayer/WBP_ModernMenu_Player.WBP_ModernMenu_Player_C";

		std::atomic<bool>  g_shown{ false };
		std::atomic<float> g_scale{ 1.0f };
		std::atomic<bool>  g_hover{ false };

		reflect::Handle g_player, g_root, g_canvas, g_image, g_slot, g_texture, g_hoverTexture;
		bool      g_failed = false;
		bool      g_drawnHover = false;
		bool      g_visible = false;
		ULONGLONG g_nextLookup = 0;

		UE::UObject* Player()
		{
			if (auto* player = reflect::Get(g_player); player && reflect::Usable(player)) return player;
			if (GetTickCount64() < g_nextLookup) return nullptr;
			g_nextLookup = GetTickCount64() + 250;
			auto* type = ui::Class(kPlayerClass);
			if (!type) return nullptr;   // the player menu has not loaded yet
			for (auto* object : reflect::Instances(type)) {
				if (reflect::Usable(object)) {
					g_player = reflect::Hold(object);
					return object;
				}
			}
			return nullptr;
		}

		// The System tab's settings menu (its page icon bar sits where the gear would be; owner 2026-11-21: no gear there).
		// CommonActivatableWidget::IsActivated, not visibility: cached switcher pages keep their visible flags.
		bool SettingsActive()
		{
			static UE::UClass* s_class = nullptr;
			if (!s_class) s_class = ui::Class(L"/Game/UI/Modern/MenuLayer/Settings/WBP_Modern_SettingsMenu.WBP_Modern_SettingsMenu_C");
			if (!s_class) return false;   // not loaded yet: the System tab was never opened
			for (auto* object : reflect::Instances(s_class)) {
				if (!reflect::Usable(object)) continue;
				ui::Call activated(object, L"IsActivated");
				if (activated && activated.Run() && activated.Get<bool>("ReturnValue")) return true;
			}
			return false;
		}

		void SetTexture(UE::UObject* a_texture)
		{
			ui::Call brush(reflect::Get(g_image), L"SetBrushFromTexture");
			brush.Set("Texture", a_texture);
			brush.Set("bMatchSize", false);
			brush.Run();
		}

		bool Build()
		{
			if (auto* root = reflect::Get(g_root); root && reflect::Get(g_image) && reflect::Get(g_slot)) {
				ui::Call inViewport(root, L"IsInViewport");
				if (inViewport.Run() && !inViewport.Get<bool>("ReturnValue")) {
					ui::Call add(root, L"AddToViewport");
					add.Set<std::int32_t>("ZOrder", 65);
					add.Run();
				}
				return true;
			}
			auto* texture = reflect::Get(g_texture);
			auto* hover = reflect::Get(g_hoverTexture);
			const auto skin = mrconfig::PluginFolder() / L"MenusRevamped" / L"Skin";
			if (!texture && (texture = ui::ImportTexture(skin / L"BuilderGear.png"))) g_texture = reflect::Hold(texture);
			if (!hover && (hover = ui::ImportTexture(skin / L"BuilderGearHover.png"))) g_hoverTexture = reflect::Hold(hover);
			if (!texture || !hover) {
				g_failed = true;
				logger::warn("builder gear: MenusRevamped/Skin/BuilderGear.png or BuilderGearHover.png is missing - no gear (F11 still opens the builder)");
				return false;
			}
			auto* root = ui::CreateWidget(L"/Script/UMG.UserWidget");
			auto* canvas = root ? ui::RootCanvas(root, L"MRBuilderGearCanvas") : nullptr;
			if (!canvas) return false;
			UE::UObject* slot = nullptr;
			auto* image = ui::AddToCanvas(canvas, L"/Script/UMG.Image", L"MRBuilderGear", &slot);
			if (!image || !slot) return false;
			ui::Call add(root, L"AddToViewport");
			add.Set<std::int32_t>("ZOrder", 65);
			add.Run();
			ui::Anchors(slot, 0.5, 0, 0.5, 0);   // from the screen's horizontal centre, like the centred player menu
			ui::Vec2(slot, L"SetAlignment", 0.5, 0.5);
			ui::Vec2(slot, L"SetPosition", ingame::kGearOffsetX, ingame::kGearCentreY);
			ui::Vec2(slot, L"SetSize", ingame::kGearSize, ingame::kGearSize);
			g_root = reflect::Hold(root);
			g_canvas = reflect::Hold(canvas);
			g_image = reflect::Hold(image);
			g_slot = reflect::Hold(slot);
			SetTexture(texture);
			g_drawnHover = false;
			ui::Visible(image, false);
			g_visible = false;
			logger::warn("{}: builder gear added to the player menu", "MR-INGAME-BUILDER-P1");
			return true;
		}

		void Hide()
		{
			g_shown = false;
			if (g_visible) {
				if (auto* image = reflect::Get(g_image)) ui::Visible(image, false);
				g_visible = false;
			}
		}
	}

	void Tick()
	{
		if (g_failed) return;
		static ULONGLONG s_next = 0;   // the visibility walk runs every 50 ms; the hover swap every tick
		if (GetTickCount64() >= s_next) {
			s_next = GetTickCount64() + 50;
			auto* player = menus::AnyOpen() ? Player() : nullptr;
			if (!player || !ui::ShownOnScreen(player) || SettingsActive() || !Build()) {
				Hide();
				return;
			}
			g_scale = ui::ViewportScale();
		} else if (!g_visible && !g_shown.load()) {
			return;
		}
		if (!reflect::Get(g_image)) {
			Hide();
			return;
		}
		if (!g_visible) {
			ui::Visible(reflect::Get(g_image), true);
			g_visible = true;
		}
		const bool hover = g_hover.load();
		if (hover != g_drawnHover) {
			SetTexture(reflect::Get(hover ? g_hoverTexture : g_texture));
			g_drawnHover = hover;
		}
		g_shown = true;
	}

	bool Hit(HWND a_window, LPARAM a_position)
	{
		if (!g_shown.load()) return false;
		RECT client{};
		if (!GetClientRect(a_window, &client)) return false;
		const auto rect = ingame::GearRect(static_cast<double>(client.right - client.left), g_scale.load());
		return rect.Contains(static_cast<double>(static_cast<short>(LOWORD(a_position))), static_cast<double>(static_cast<short>(HIWORD(a_position))));
	}

	void Hover(bool a_over) { g_hover = a_over; }
}
