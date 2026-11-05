#include "alchemy/AlchemyInternal.h"

namespace alchemy
{
	namespace detail
	{

		// ---- IMP-MR-005: sort
		std::string ReadPotionName(UE::UObject* a_name)
		{
			ui::Call get(a_name, L"GetTextValue");
			auto* result = get ? get.At("ReturnValue") : nullptr;
			if (!result || get.Size("ReturnValue") != sizeof(UE::FText) || !get.Run()) return {};
			auto* text = static_cast<UE::FText*>(result);
			const auto value = reflect::Text(*text);
			text->~FText();
			return value;
		}

		bool NameTextCall(UE::UObject* a_name, const wchar_t* a_function, const char* a_parameter, const std::string& a_value, bool a_commit)
		{
			ui::Call call(a_name, a_function);
			auto* parameter = call ? call.At(a_parameter) : nullptr;
			if (!parameter || call.Size(a_parameter) != sizeof(UE::FText)) return false;
			const auto wide = textutil::Wide(a_value);
			auto* text = new (parameter) UE::FText(UE::FText::AsCultureInvariant(UE::FString(wide.c_str())));
			if (a_commit) call.Set("CommittedMethod", std::uint8_t{ 1 }); // ETextCommit::OnEnter
			const bool ran = call.Run();
			text->~FText();
			return ran;
		}

		void PotionNameTick(UE::UObject* a_name)
		{
			ui::Call enabled(a_name, L"GetIsEnabled");
			g_canName = a_name && ui::ShownOnScreen(a_name) && enabled && enabled.Run() && enabled.Get<bool>("ReturnValue");
			if (g_nameKeyboardRequest.exchange(false) && g_canName.load() && !keyboard::Taking()) {
				g_nameApplied = ReadPotionName(a_name);
				search::SetText(menus::Menu::kAlchemyName, g_nameApplied);
				ui::Call start(a_name, L"StartEditText"); if (start) start.Run();
				g_nameMode = true;
				if (!keyboard::Open(menus::Menu::kAlchemyName, kPadName)) {
					g_nameMode = false;
					ui::Call stop(a_name, L"StopEditText"); if (stop) stop.Run();
				}
			}
			if (!g_nameMode.load()) return;
			const auto value = search::GetText(menus::Menu::kAlchemyName);
			const bool commit = !keyboard::Taking();
			if (value != g_nameApplied || commit) {
				const bool set = NameTextCall(a_name, L"SetUserInputText", "InText", value);
				const bool change = NameTextCall(a_name, L"UpdateOnTextChanged", "NewUserInputText", value);
				const bool done = !commit || NameTextCall(a_name, L"Update on Text Committed", "NewText", value, true);
				if (set && change && done) g_nameApplied = value;
				else { static bool said = false; if (!said) { said = true; logger::warn("alchemy: potion-name text bridge unavailable"); } }
			}
			if (commit) {
				ui::Call stop(a_name, L"StopEditText"); if (stop) stop.Run();
				g_nameMode = false;
				logger::debug("alchemy: controller potion name committed");
			}
		}


		// ---- the menu
		int MoveChipFocus(int a_index, pickerpad::Direction a_direction)
		{
			const int dx = a_direction == pickerpad::Direction::Left ? -1 : a_direction == pickerpad::Direction::Right ? 1 : 0;
			const int dy = a_direction == pickerpad::Direction::Up ? -1 : a_direction == pickerpad::Direction::Down ? 1 : 0;
			std::scoped_lock lock(g_rectLock);
			return ChipStep(g_chipRects, a_index, dx, dy, g_chipCount.load());
		}

		void ApplySortInput()
		{
			if (const int icon = g_iconClick.exchange(-1); icon >= 0 && icon < kIcons) {
				g_sortState = Click(g_sortState, static_cast<SortKey>(icon));
				g_dirtySort = true;
				g_nextLayout = {};
			}
			if (const int direction = g_sortDirection.exchange(-1); direction >= 0 && direction < kIcons * 2) {
				g_sortState = { static_cast<SortKey>(direction / 2), (direction & 1) != 0 };
				g_dirtySort = true;
				g_nextLayout = {};
			}
		}
	}

	using namespace detail;

	bool Message(UINT a_message, WPARAM a_key, LPARAM a_position)
	{
		if (!g_openFlag.load() || !Enabled() || search::Typing() || keyboard::Taking()) return false;
		if (g_filtersVisible.load() && a_message == WM_KEYDOWN && (a_key == 'A' || a_key == 'D')) {
			if (!(a_position & (1 << 30))) g_tabCycle = a_key == 'A' ? -1 : 1;
			return true;
		}
		if (a_message == WM_LBUTTONDOWN) {
			const double x = static_cast<short>(LOWORD(a_position)), y = static_cast<short>(HIWORD(a_position));
			std::scoped_lock lock(g_rectLock);
			if (g_filtersVisible.load()) {
				for (int i = 0; i < 3; ++i) if (g_tabRects[i].Has(x, y)) {
					g_tabRequest = i;
					g_swallowUp = true;
					return true;
				}
			}
			for (int i = 0; i < kIcons; ++i) if (g_iconRects[static_cast<std::size_t>(i)].Has(x, y)) {
				g_iconClick = i;
				g_swallowUp = true;
				return true;
			}
			for (std::size_t i = 0; i < g_chipRects.size(); ++i) {
				if (g_chipRects[i].Has(x, y)) {
					g_chipClick = static_cast<int>(i);
					g_swallowUp = true;
					return true;
				}
			}
			return false;
		}
		if (a_message == WM_MOUSEMOVE) {
			const double     x = static_cast<short>(LOWORD(a_position)), y = static_cast<short>(HIWORD(a_position));
			std::vector<Box> boxes;
			{
				std::scoped_lock lock(g_rectLock);
				for (const auto& r : g_chipRects) boxes.push_back(r.ok ? Box{ r.x, r.y, r.w, r.h } : Box{});
			}
			int tabHit = -1;
			{
				std::scoped_lock lock(g_rectLock);
				if (g_filtersVisible.load()) for (int i = 0; i < 3; ++i) if (g_tabRects[i].Has(x, y)) { tabHit = i; break; }
			}
			g_hoverTab = tabHit;
			int iconHit = -1;
			{
				std::scoped_lock lock(g_rectLock);
				for (int i = 0; i < kIcons; ++i) if (g_iconRects[static_cast<std::size_t>(i)].Has(x, y)) { iconHit = i; break; }
			}
			if (g_hoverIcon.exchange(iconHit) != iconHit) g_headerNow = true;
			const int hit = HitIndex(boxes, x, y);
			if (g_hoverChip.exchange(hit) != hit) g_hoverNow = true;
			return false;   // the game still sees the mouse move
		}
		if (a_message == WM_LBUTTONUP && g_swallowUp) {
			g_swallowUp = false;
			return true;
		}
		return false;   // no sort key: the header icons are clicked (owner 2026-11-04)
	}

	// Native X/Create and Y/Filters (hold Y/Clear all) pass through unchanged.
	// LB selects filter chips; RB selects sort icons. Native panels/text input own their controls.
	// Buttons consumed by a custom mode remain suppressed until released.
	void Controller(std::uint16_t& a_buttons, short& a_lx, short& a_ly)
	{
		const pickerpad::Input raw{ a_buttons, a_lx, a_ly };
		g_sortNav.Sample(raw, GetTickCount64());
		const auto filterStick = [&] {
			const auto filtered = g_sortNav.Filter(raw);
			a_lx = filtered.x; a_ly = filtered.y;
		};
		g_padSwallow &= a_buttons;
		const std::uint16_t pressed = a_buttons & static_cast<std::uint16_t>(~g_lastButtons);
		g_lastButtons = a_buttons;
		if (g_openFlag.load() && Enabled() && !g_filtersVisible.load() && !search::Typing() && !keyboard::Taking() &&
			g_padFocus.load() < 0 && g_chipFocus.load() < 0 && (pressed & kPadName) && g_canName.load()) {
			g_nameKeyboardRequest = true;
			g_padSwallow |= kPadName;
		}
		if (!g_openFlag.load() || !Enabled() || g_filtersVisible.load() || g_nameTyping.load() || search::Typing() || keyboard::Taking()) {
			if (g_padFocus.exchange(-1) >= 0) g_headerNow = true;
			if (g_chipFocus.exchange(-1) >= 0) g_hoverNow = true;
			a_buttons &= static_cast<std::uint16_t>(~g_padSwallow);
			filterStick();
			return;
		}
		if (pressed & kPadRB) {
			g_padFocus = g_padFocus.load() >= 0 ? -1 : g_activeIcon.load();
			g_chipFocus = -1;
			g_headerNow = g_hoverNow = true;
		} else if (pressed & kPadLB) {
			g_chipFocus = g_chipFocus.load() >= 0 || g_chipCount.load() == 0 ? -1 : 0;
			g_padFocus = -1;
			g_headerNow = g_hoverNow = true;
		}
		g_padSwallow |= static_cast<std::uint16_t>(a_buttons & (kPadLB | kPadRB));
		const int sortFocus = g_padFocus.load(), chipFocus = g_chipFocus.load();
		if (sortFocus >= 0 || chipFocus >= 0) {
			const auto direction = g_sortNav.pulse;
			g_sortNav.Consume(raw, 0, true);
			if (pressed & kPadB) {
				g_padFocus = g_chipFocus = -1;
				g_headerNow = g_hoverNow = true;
			} else if (sortFocus >= 0 && (direction == pickerpad::Direction::Left || direction == pickerpad::Direction::Right)) {
				g_padFocus = SortNavStep(sortFocus, direction == pickerpad::Direction::Left ? -1 : 1);
				g_headerNow = true;
			} else if (sortFocus >= 0 && (direction == pickerpad::Direction::Up || direction == pickerpad::Direction::Down)) {
				g_sortDirection = sortFocus * 2 + (direction == pickerpad::Direction::Down ? 1 : 0);
			} else if (chipFocus >= 0 && direction != pickerpad::Direction::None) {
				g_chipFocus = MoveChipFocus(chipFocus, direction);
				g_hoverNow = true;
			} else if (pressed & kPadA) {
				if (sortFocus >= 0) g_iconClick = sortFocus;
				else g_chipClick = chipFocus;
			}
			g_padSwallow |= static_cast<std::uint16_t>(a_buttons & (kPadUp | kPadDown | kPadLeft | kPadRight | kPadA | kPadB));
		}
		a_buttons &= static_cast<std::uint16_t>(~g_padSwallow);
		filterStick();
	}
}
