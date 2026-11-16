#include "enchanting/EnchantingName.h"
#include "engine/Menus.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/TextUtil.h"
#include "engine/Ui.h"
#include "keyboard/Keyboard.h"
#include "search/Search.h"

namespace enchanting
{
	namespace
	{
		constexpr std::uint16_t kPadName = 0x0040;   // L3, the Alchemy name button (XINPUT_GAMEPAD_LEFT_THUMB)

		struct Altar
		{
			const wchar_t*  path;
			const char*     box;
			menus::Menu     target;
			UE::UClass*     cls = nullptr;
			reflect::Handle menu;
			ULONGLONG       next = 0;
		};
		Altar g_altars[] = {
			{ L"/Game/UI/Modern/GameMenuLayer/enchantment/WBP_ModernMenu_EnchantmentMenu.WBP_ModernMenu_EnchantmentMenu_C",
			  "EnchantedItem_NameBox", menus::Menu::kEnchantName },
			{ L"/Game/UI/Modern/GameMenuLayer/Spellmaking/WBP_ModernMenu_SpellMakingMenu.WBP_ModernMenu_SpellMakingMenu_C",
			  "SpellMakingItem_NameBox", menus::Menu::kSpellName },
			{ L"/Game/UI/Modern/GameMenuLayer/sigilstone/WBP_ModernMenu_SigilStoneMenu.WBP_ModernMenu_SigilStoneMenu_C",
			  "EnchantedItem_NameBox", menus::Menu::kEnchantName },   // C4-11: the same rename switch and name box
		};
		std::atomic<int>  g_openAltar{ -1 };
		std::atomic<bool> g_canName{ false }, g_nameMode{ false }, g_nameKeyboardRequest{ false };
		std::string       g_nameApplied, g_nameInitial;
		std::uint16_t     g_lastButtons = 0, g_padSwallow = 0;

		UE::UObject* Field(UE::UObject* a_obj, std::string_view a_name)
		{
			if (!a_obj || !a_obj->GetClass()) return nullptr;
			const auto off = reflect::Offset(a_obj->GetClass(), a_name);
			if (off < 0) return nullptr;
			auto* p = *reflect::At<UE::UObject*>(a_obj, off);
			return p && reflect::IsLive(p) ? p : nullptr;
		}

		bool Active(UE::UObject* a_menu)
		{
			const auto off = a_menu && a_menu->GetClass() ? reflect::Offset(a_menu->GetClass(), "bIsActive") : -1;
			return off >= 0 && *reflect::At<bool>(a_menu, off);
		}

		std::string ReadName(UE::UObject* a_box)
		{
			ui::Call get(a_box, L"GetTextValue");
			auto* result = get ? get.At("ReturnValue") : nullptr;
			if (!result || get.Size("ReturnValue") != sizeof(UE::FText) || !get.Run()) return {};
			auto* text = static_cast<UE::FText*>(result);
			const auto value = reflect::Text(*text);
			text->~FText();
			return value;
		}

		bool NameTextCall(UE::UObject* a_box, const wchar_t* a_function, const char* a_parameter, const std::string& a_value, bool a_commit = false)
		{
			ui::Call call(a_box, a_function);
			auto* parameter = call ? call.At(a_parameter) : nullptr;
			if (!parameter || call.Size(a_parameter) != sizeof(UE::FText)) return false;
			const auto wide = textutil::Wide(a_value);
			auto* text = new (parameter) UE::FText(UE::FText::AsCultureInvariant(UE::FString(wide.c_str())));
			if (a_commit) call.Set("CommittedMethod", std::uint8_t{ 1 });   // ETextCommit::OnEnter
			const bool ran = call.Run();
			text->~FText();
			return ran;
		}

		// the menu's own rename switch (Is RenameBoxFocused, panel focus, StartEditText / StopEditText)
		bool RenameFocus(UE::UObject* a_menu, bool a_on)
		{
			ui::Call call(a_menu, L"UpdateRenameBoxFocus");
			if (!call || !call.At("New Focus State")) return false;
			call.Set("New Focus State", a_on);
			return call.Run();
		}

		void EnchantNameTick(UE::UObject* a_menu, UE::UObject* a_box, menus::Menu a_target)
		{
			ui::Call enabled(a_box, L"GetIsEnabled");
			g_canName = a_box && ui::ShownOnScreen(a_box) && enabled && enabled.Run() && enabled.Get<bool>("ReturnValue");
			if (g_nameKeyboardRequest.exchange(false) && g_canName.load() && !keyboard::Taking()) {
				g_nameApplied = ReadName(a_box);
				g_nameInitial = g_nameApplied;
				search::SetText(a_target, g_nameApplied);
				RenameFocus(a_menu, true);
				g_nameMode = true;
				if (!keyboard::Open(a_target, kPadName)) {
					g_nameMode = false;
					RenameFocus(a_menu, false);
				}
			}
			if (!g_nameMode.load()) return;
			const auto value = search::GetText(a_target);
			const bool commit = !keyboard::Taking();
			if (value != g_nameApplied || commit) {
				const bool set = NameTextCall(a_box, L"SetUserInputText", "InText", value);
				const bool change = NameTextCall(a_box, L"UpdateOnTextChanged", "NewUserInputText", value);
				const bool done = !commit || NameTextCall(a_box, L"Update on Text Committed", "NewText", value, true);
				if (set && change && done) g_nameApplied = value;
				else { static bool said = false; if (!said) { said = true; logger::warn("enchanting: item-name text bridge unavailable"); } }
			}
			if (commit) {
				if (!RenameFocus(a_menu, false)) {
					static bool warned = false;
					if (!warned) { warned = true; logger::warn("enchanting: UpdateRenameBoxFocus unavailable"); }
				}
				g_nameMode = false;
			}
		}

		void OnMenuEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void*)
		{
			for (auto& altar : g_altars) {
				if (!altar.cls || a_obj->GetClass() != altar.cls) continue;
				if (pe::FunctionName(a_fn) == "Destruct") { if (reflect::Get(altar.menu) == a_obj) altar.menu = {}; return; }
				if (reflect::Get(altar.menu) != a_obj && reflect::IsLive(a_obj)) altar.menu = reflect::Hold(a_obj);
				return;
			}
		}

		UE::UObject* Menu(Altar& a_altar)
		{
			if (!a_altar.cls) {
				if (GetTickCount64() < a_altar.next || !menus::AnyOpen()) return nullptr;
				a_altar.next = GetTickCount64() + 500;
				a_altar.cls = ui::Class(a_altar.path);   // loaded once the game first opens the menu
				if (!a_altar.cls) return nullptr;
				pe::WatchPost(a_altar.cls, &OnMenuEvent);
				for (auto* w : reflect::Instances(a_altar.cls)) {   // once: already open when its class first loads
					if (Active(w)) { a_altar.menu = reflect::Hold(w); break; }
				}
			}
			return reflect::Get(a_altar.menu);
		}
	}

	menus::Menu NamingMenu()
	{
		const int open = g_openAltar.load();
		return open >= 0 && g_nameMode.load() ? g_altars[open].target : menus::Menu::kNone;
	}

	void Tick()
	{
		if (!reflect::Ok()) return;
		int          open = -1;
		UE::UObject* menu = nullptr;
		for (int i = 0; i < static_cast<int>(std::size(g_altars)) && open < 0; ++i) {
			auto* m = Menu(g_altars[i]);
			if (m && Active(m)) { open = i; menu = m; }
		}
		if (open != g_openAltar.load()) {
			g_openAltar = open;
			g_nameMode = false; g_nameKeyboardRequest = false; g_canName = false;
		}
		if (open < 0) return;
		EnchantNameTick(menu, Field(menu, g_altars[open].box), g_altars[open].target);
	}

	void Controller(std::uint16_t& a_buttons, short&, short&)
	{
		g_padSwallow &= a_buttons;
		const std::uint16_t pressed = a_buttons & static_cast<std::uint16_t>(~g_lastButtons);
		g_lastButtons = a_buttons;
		if (g_openAltar.load() >= 0 && !search::Typing() && !keyboard::Taking() && (pressed & kPadName) && g_canName.load()) {
			g_nameKeyboardRequest = true;
			g_padSwallow |= kPadName;   // the game never sees this L3 (its own rename would toggle the box again)
		}
		a_buttons &= static_cast<std::uint16_t>(~g_padSwallow);
	}
}
