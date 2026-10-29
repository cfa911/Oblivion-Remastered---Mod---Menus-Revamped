#include "search/SearchInternal.h"

#include "search/SearchConfig.h"
#include "engine/ContainerMenu.h"
#include "engine/PEHook.h"
#include "engine/QuestsMenu.h"
#include "engine/Reflect.h"
#include "engine/TextUtil.h"
#include "engine/Ui.h"
#include "engine/UiText.h"

namespace search::detail
{
	// a list item's name: an FText-returning accessor if it has one, else the Properties struct's Name (as the row widgets carry it)
	// a name for a row of a kind we do not know the properties of (the quests tab): a name accessor if it has one, else the item's own text properties
	// (the ones called ...Name... or ...Title... first); the class and its text properties are logged once
	std::string GenericItemName(UE::UObject* a_item, const char** a_route)
	{
		static bool s_logged = false;
		std::vector<std::pair<std::string, std::string>> texts;   // property name, its text
		{   // the row's GetProperties() struct (LegacyMapMenuQuestProperties): its text properties
			static auto* props = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Altar.LegacyMapMenuQuestProperties"));
			static bool  s_said = false;
			ui::Call     g(a_item, L"GetProperties");
			void*        ret = g && props ? g.At("ReturnValue") : nullptr;
			if (!s_said) {
				s_said = true;
				logger::debug("own filter (quests): GetProperties {}, the struct {}, its size {} / the call's {}", g ? "exists" : "is missing", props ? "is there" : "is missing", props ? props->propertiesSize : -1, g && props ? g.Size("ReturnValue") : -1);
				if (props) {
					for (const auto& [name, off] : reflect::Fields(props)) logger::debug("own filter (quests):   {}.{} @0x{:X} size {}", "LegacyMapMenuQuestProperties", name, off, ui::SizeOf(props, name));
				}
			}
			if (ret && g.Size("ReturnValue") == props->propertiesSize && g.Run()) {
				for (const auto& [name, off] : reflect::Fields(props)) {
					const auto size = ui::SizeOf(props, name);
					if (off < 0 || size != static_cast<std::int32_t>(sizeof(UE::FText)) || off + size > props->propertiesSize) continue;
					auto* t = reinterpret_cast<UE::FText*>(static_cast<std::uint8_t*>(ret) + off);
					if (reflect::TextSet(*t)) texts.emplace_back(name, reflect::Text(*t));
					t->~FText();
				}
			}
		}
for (UE::UStruct* c = a_item->GetClass(); c; c = c->GetSuperStruct()) {
			if (pe::Utf8(c->GetFName().ToString()) == "Object") break;
			for (const auto& [name, off] : reflect::Fields(c)) {
				const auto size = ui::SizeOf(c, name);
				if (off < 0 || size != static_cast<std::int32_t>(sizeof(UE::FText)) || off + size > c->propertiesSize) continue;
				const auto& t = *reinterpret_cast<const UE::FText*>(reinterpret_cast<const std::uint8_t*>(a_item) + off);
				if (!reflect::TextSet(t)) continue;
				texts.emplace_back(name, reflect::Text(t));
			}
		}
		if (!s_logged) {
			s_logged = true;
			logger::debug("own filter (quests): a list item is {}; its text properties: {}", pe::Utf8(a_item->GetClass()->GetFName().ToString()), texts.empty() ? std::string("none") : std::string());
			for (const auto& [n, t] : texts) logger::debug("own filter (quests):   {} = {}", n, t.substr(0, 60));
		}
		for (const char* want : { "name", "title" }) {
			for (const auto& [n, t] : texts) {
				std::string low = n;
				for (auto& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
				if (!t.empty() && low.find(want) != std::string::npos) {
					if (a_route) *a_route = "text property";
					return t;
				}
			}
		}
		std::string all;
		for (const auto& [n, t] : texts) all += t + " ";
		if (!all.empty() && a_route) *a_route = "text properties";
		return all;
	}

	std::string ItemName(UE::UObject* a_item, menus::Menu a_menu, const char** a_route)
{
		static auto* magicProps = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Altar.LegacyMagicMenuItemProperties"));
		static auto* invProps = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Altar.OriginalInventoryMenuItemProperties"));
		if (!a_item || !a_item->GetClass() || !reflect::Ok() || !reflect::IsLive(a_item)) return {};
		for (const wchar_t* fn : { L"GetNameText", L"GetDisplayNameText" }) {
			ui::Call c(a_item, fn);
			void*    ret = c ? c.At("ReturnValue") : nullptr;
			if (!ret || c.Size("ReturnValue") != static_cast<std::int32_t>(sizeof(UE::FText)) || !c.Run()) continue;
			auto*       t = static_cast<UE::FText*>(ret);
			std::string s = reflect::Text(*t);
			t->~FText();
			if (!s.empty()) {
				if (a_route) *a_route = fn[3] == L'N' ? "GetNameText" : "GetDisplayNameText";
				return s;
			}
		}
		if (a_menu == menus::Menu::kQuests) return GenericItemName(a_item, a_route);
					auto* props = a_menu == menus::Menu::kMagic ? magicProps : invProps;
		ui::Call g(a_item, L"GetProperties");
		void*    ret = g && props ? g.At("ReturnValue") : nullptr;
		const auto nameOff = props ? reflect::Offset(props, "Name") : -1;
		if (!ret || nameOff < 0 || g.Size("ReturnValue") != props->propertiesSize || !g.Run()) return {};
		auto*       t = reinterpret_cast<UE::FText*>(static_cast<std::uint8_t*>(ret) + nameOff);
		std::string s = reflect::Text(*t);
		t->~FText();
		if (a_route && !s.empty()) *a_route = "GetProperties";
		return s;
	}

	std::string Lower(const std::string& a_s)
	{
		return textutil::Lower(a_s);
	}

	// does an item's name hold the search text (a_query is Lower() of what was typed)?
	bool Matches(UE::UObject* a_item, menus::Menu a_menu, const std::string& a_query)
	{
		return textutil::Contains(ItemName(a_item, a_menu), a_query, searchcfg::Get().ignoreAccents);
	}
}
