#include "sorting/PickerInternal.h"

#include "core/Runtime.h"
#include "engine/ContainerMenu.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/TextUtil.h"
#include "engine/Ui.h"
#include "engine/UiText.h"
#include "sorting/GearData.h"
#include "sorting/Icons.h"

namespace sorting
{
	namespace detail
	{
		// Native tab (page byte) of a container/barter side: kind 3 player, 4 container/merchant; -1 when not readable.
		int ContainerPage(int a_kind)
		{
			auto* menu = container::Widget();
			auto** vm = menu ? reflect::At<UE::UObject*>(menu, reflect::Offset(menu->GetClass(), "VContainerMenuViewModel")) : nullptr;
			auto* page = vm && *vm ? reflect::At<std::uint8_t>(*vm, reflect::Offset((*vm)->GetClass(),
				a_kind == 3 ? "CurrentPageInventory" : "CurrentPageContainer")) : nullptr;
			return page ? *page : -1;
		}

		int Index(UE::UObject* a_item)
		{
			ui::Call get(a_item, L"GetInventoryIndex");
			return get && get.Size("ReturnValue") == 4 && get.Run() ? get.Get<int>("ReturnValue") : -1;
		}

		static std::uint32_t FormID(UE::UObject* form)
		{
			// UTESForm::m_formID +0x28, recorded in docs/inventory/internals.md
			// for game 1.512.105.0. This native member need not be a UPROPERTY.
			__try { return form ? *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(form) + 0x28) : 0; }
			__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
		}

		// The Inventory / container row's GetProperties() struct (native, so it never goes away).
		static UE::UStruct* ItemProperties()
		{
			static auto* properties = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(
				nullptr, nullptr, L"/Script/Altar.OriginalInventoryMenuItemProperties"));
			return properties;
		}

		std::uint32_t ContainerForm(UE::UObject* item, std::uint64_t* a_key)
		{
			auto* properties = ItemProperties();
			ui::Call get(item, L"GetProperties");
			auto* result = get.At("ReturnValue");
			const auto formOffset = properties ? reflect::Offset(properties, "form") : -1;
			const auto nameOffset = properties ? reflect::Offset(properties, "Name") : -1;
			if (!result || formOffset < 0 || nameOffset < 0 ||
				get.Size("ReturnValue") != properties->propertiesSize || !get.Run()) return 0;
			auto* form = *reinterpret_cast<UE::UObject**>(static_cast<std::uint8_t*>(result) + formOffset);
			const auto value = FormID(form);
			auto* name = reinterpret_cast<UE::FText*>(static_cast<std::uint8_t*>(result) + nameOffset);
			if (a_key) {
				static const auto weightOffset = reflect::Offset(properties, "Weight"), typeOffset = reflect::Offset(properties, "Type");
				std::string key = value ? std::to_string(value) : reflect::Text(*name);
				if (!value && weightOffset >= 0 && typeOffset >= 0) {
					key.append(reinterpret_cast<const char*>(static_cast<std::uint8_t*>(result) + weightOffset), sizeof(float));
					key.push_back(static_cast<char>(*(static_cast<std::uint8_t*>(result) + typeOffset)));
				}
				*a_key = key.empty() ? 0 : std::hash<std::string>{}(key) | 1;
			}
			name->~FText();
			return value;
		}

		stolenfilter::Status StolenStatus(UE::UObject* item)
		{
			auto* properties = ItemProperties();
			ui::Call get(item, L"GetProperties");
			auto* result = get.At("ReturnValue");
			const auto flagsOffset = properties ? reflect::Offset(properties, "StatusFlags") : -1;
			const auto nameOffset = properties ? reflect::Offset(properties, "Name") : -1;
			if (!result || flagsOffset < 0 || nameOffset < 0 || ui::SizeOf(properties, "StatusFlags") != 4 ||
				get.Size("ReturnValue") != properties->propertiesSize || !get.Run()) return stolenfilter::Status::Unknown;
			std::uint32_t flags = 0;
			std::memcpy(&flags, static_cast<std::uint8_t*>(result) + flagsOffset, sizeof(flags));
			reinterpret_cast<UE::FText*>(static_cast<std::uint8_t*>(result) + nameOffset)->~FText();
			return stolenfilter::FromFlags(flags);
		}

		// ---- Magic rows: school/range from the row, native spell type/effects from the player's spell list ----
		// Row layout (VLegacyMagicMenuItem, 136 bytes, proven 2026-10-24): Category +0x50, Type +0x51, InventoryIndex +0x54, School +0x58.
		// InventoryIndex - 1 is the spell's position in the player's list.
		using SpellRow = magicrows::Spell;
		static_assert(static_cast<unsigned>(RE::MagicSystem::SpellType::kSpell) == 0);
		static_assert(static_cast<unsigned>(RE::MagicSystem::SpellType::kPower) == 2);
		static_assert(static_cast<unsigned>(RE::MagicSystem::SpellType::kLesserPower) == 3);

		static int ReadSpellsRaw(SpellRow* a_out, int a_max)
		{
			int n = 0;
			__try {
				auto* player = RE::PlayerCharacter::GetSingleton();
				auto* base = player ? player->data.objectReference : nullptr;
				if (!base || !base->Is(RE::FormType::NPC)) return 0;
				auto* list = static_cast<RE::TESSpellList*>(static_cast<RE::TESNPC*>(base));
				using Node = RE::BSSimpleList<RE::SpellItem*>::Node;
				for (auto* node = reinterpret_cast<Node*>(&list->spells); node && n < a_max; node = node->next) {
					auto* spell = node->item;
					SpellRow row{};
					if (spell && spell->Is(RE::FormType::Spell)) {
						row.type = static_cast<unsigned>(spell->data.spellType);
						auto** effect = reinterpret_cast<void**>(&spell->effectList);
						for (int k = 0; effect && k < 8; ++k) {
							auto* item = reinterpret_cast<std::uint8_t*>(effect[0]);
							if (item) row.codes[row.count++] = *reinterpret_cast<std::uint32_t*>(item + 0x08);
							effect = reinterpret_cast<void**>(effect[1]);
						}
						row.valid = true;
					}
					a_out[n++] = row;
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
			return n;
		}

		static bool ReadByte(const void* a_object, std::size_t a_offset, std::uint8_t& a_value)
		{
			__try { a_value = *(reinterpret_cast<const std::uint8_t*>(a_object) + a_offset); return true; }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}

		void ComputeMagicMasks(const std::vector<UE::UObject*>& a_items, FilterState& state)
		{
			std::vector<SpellRow> spells(600);
			spells.resize(static_cast<std::size_t>(ReadSpellsRaw(spells.data(), 600)));
			for (auto* item : a_items) {
				const int index = Index(item);
				std::uint8_t range = 0, school = 0;
				const bool read = item && ReadByte(item, 0x51, range) && ReadByte(item, 0x58, school);
				const auto mask = read ? magicrows::Mask(range, school, index, spells.data(), spells.size()) : kPassAll;
				// A genuine unresolved row still fails open; report once, never every row/menu tick.
				static bool warned = false;
				if (!warned && read && magicrows::CastableRange(range) && mask == kPassAll) {
					warned = true;
					logger::warn("Menus Revamped: native spell-type power filtering could not resolve a Magic row (index={}, range={}, captured={}); leaving it unfiltered", index, range, spells.size());
				}
				state.masks[index] = mask;
			}
		}
	}
}
