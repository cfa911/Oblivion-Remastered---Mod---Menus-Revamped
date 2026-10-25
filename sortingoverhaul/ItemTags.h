#pragma once

#include "GearPieces.h"
#include "MagicEffects.h"
#include "PickerGrid.h"

#include <array>
#include <cstdint>
#include <string_view>

// Per-tab picker choices and the pure classifiers that turn a native item into "tag" bits.
// A tag bit k means "belongs to choice k + 1 of the item's own tab"; choice 0 is always All.
// Page numbers (CurrentPageInventory, proven in game 1.512.105.0): 0 weapons, 1 gear, 2 potions, 3 misc, 4 all items.
namespace itemtags
{
	enum class Tab : unsigned { Weapons, Gear, Potions, Misc, AllItems, MagicSchool, MagicEffect };
	constexpr unsigned kMaxChoices = pickergrid::kMaxChoices;
	static_assert(magiceffects::kChoiceCount <= kMaxChoices);

	// Only the Magic School popup uses these short display labels. TabInfo names stay
	// complete for tooltips, selection identity, and all other pickers.
	inline constexpr std::array<std::string_view, 8> kSchoolPopupLabels{
		"All", "Alte", "Conj", "Dest", "Illu", "Myst", "Rest", "Powers"
	};
	constexpr std::string_view SchoolPopupLabel(unsigned choice)
	{
		return choice < kSchoolPopupLabels.size() ? kSchoolPopupLabels[choice] : std::string_view{};
	}

	struct TabInfo
	{
		const char*    title;
		unsigned       count;  // including All
		const char*    names[kMaxChoices];
		const wchar_t* icons[kMaxChoices];  // PNG filenames in OBSE/Plugins/SortingOverhaul/
	};

	inline const TabInfo& Info(Tab a_tab)
	{
		static const TabInfo tabs[] = {
			{ "Weapon type", 5, { "All", "Blade", "Blunt", "Bow", "Staff" },
				{ L"Weapon-All.png", L"Weapon-Blade.png", L"Weapon-Blunt.png", L"Weapon-Bow.png", L"Weapon-Staff.png" } },
			{ "Equipment piece", 9, { "All", "Ring", "Amulet", "Head", "Torso", "Arms", "Feet", "Legwear", "Shield" },
				{ L"Gear-All.png", L"Gear-Ring.png", L"Gear-Amulet.png", L"Gear-Head.png", L"Gear-Torso.png", L"Gear-Arms.png", L"Gear-Feet.png", L"Gear-Legwear.png", L"Gear-Shield.png" } },
			{ "Potion type", 6, { "All", "Health", "Magicka", "Fatigue", "Cure", "Poison" },
				{ L"Potion-All.png", L"Potion-Health.png", L"Potion-Magicka.png", L"Potion-Fatigue.png", L"Potion-Cure.png", L"Potion-Poison.png" } },
			{ "Item type", 6, { "All", "Keys", "Soul gems", "Books", "Tools", "Other" },
				{ L"Misc-All.png", L"Misc-Keys.png", L"Misc-SoulGems.png", L"Misc-Books.png", L"Misc-Tools.png", L"Misc-Other.png" } },
			// All Items: the four tab categories first; a category then opens its own choices.
			{ "Item category", 5, { "All", "Weapons", "Gear", "Potions", "Misc" },
				{ L"Category-All.png", L"Category-Weapons.png", L"Category-Gear.png", L"Category-Potions.png", L"Category-Misc.png" } },
			// Magic menu: two independent pickers (school, then effect family).
			{ "Spell school", 8, { "All", "Alteration", "Conjuration", "Destruction", "Illusion", "Mysticism", "Restoration", "Powers" },
				{ L"School-All.png", L"School-Alteration.png", L"School-Conjuration.png", L"School-Destruction.png", L"School-Illusion.png", L"School-Mysticism.png", L"School-Restoration.png", L"School-Powers.png" } },
			[] {
				TabInfo info{ "Spell effect", magiceffects::kChoiceCount, {}, {} };
				for (unsigned i = 0; i < info.count; ++i) {
					info.names[i] = magiceffects::kChoices[i].name;
					info.icons[i] = magiceffects::kChoices[i].icon;
				}
				return info;
			}(),
		};
		return tabs[static_cast<unsigned>(a_tab)];
	}

	constexpr bool TabFromPage(int a_page, Tab& a_tab)
	{
		if (a_page < 0 || a_page > 4) return false;
		a_tab = static_cast<Tab>(a_page);
		return true;
	}

	// Tag bits 0-27 are the item's choice bits on its own tab; bits 28-31 hold its category (1 weapons, 2 gear, 3 potions, 4 misc, 0 none).
	constexpr std::uint32_t kTagBits = 0x0FFFFFFFu;
	constexpr unsigned Category(unsigned a_formType)
	{
		switch (a_formType) {
		case 33: return 1;                          // weapon
		case 20: case 22: return 2;                 // armor, clothing
		case 40: case 25: case 19: return 3;        // alchemy, ingredient, apparatus
		case 27: case 26: case 42: case 38: case 21: case 39: return 4;  // misc, light, sigil stone, soul gem, book, key
		default: return 0;
		}
	}
	constexpr unsigned CategoryOf(std::uint32_t a_tags) { return a_tags >> 28; }
	constexpr bool Includes(unsigned a_choice, std::uint32_t a_tags) { return a_choice == 0 || ((a_tags & kTagBits) >> (a_choice - 1) & 1u) != 0; }
	// All Items page: a_category 0 = unfiltered, else the item must be in that category and (choice 0 = whole category) in the sub-choice.
	constexpr bool IncludesAll(unsigned a_category, unsigned a_choice, std::uint32_t a_tags)
	{
		return a_category == 0 || (CategoryOf(a_tags) == a_category && Includes(a_choice, a_tags));
	}

	// Gear: biped slot mask -> bits in the order Ring, Amulet, Head, Torso, Arms, Feet, Legwear, Shield.
	constexpr std::uint32_t GearTags(std::uint16_t a_biped)
	{
		std::uint32_t tags = 0;
		for (unsigned i = 1; i <= 8; ++i) if (gearpieces::Includes(static_cast<gearpieces::Piece>(i), a_biped)) tags |= 1u << (i - 1);
		return tags;
	}

	// Weapons: OBJ_WEAP.type (RE::WEAPON_TYPE) -> Blade, Blunt, Bow, Staff.
	constexpr std::uint32_t WeaponTags(int a_type)
	{
		switch (a_type) {
		case 0: case 1: return 1u << 0;  // blade one/two hand
		case 2: case 3: return 1u << 1;  // blunt one/two hand
		case 5: return 1u << 2;          // marksman bow
		case 4: return 1u << 3;          // staff
		default: return 0;
		}
	}

	constexpr std::uint32_t Code(char a, char b, char c, char d)
	{
		return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) | (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8) |
		       (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
	}

	// Potions: effect codes (EffectItem + 0x08, proven on 1.512.105.0) -> Health, Magicka, Fatigue, Cure, Poison.
	// A potion with several kinds of effect belongs to every matching choice; Poison means "has a hostile effect".
	constexpr std::uint32_t PotionTags(const std::uint32_t* a_codes, unsigned a_count, int a_hostileCount)
	{
		std::uint32_t tags = 0;
		for (unsigned i = 0; i < a_count; ++i) {
			const auto code = a_codes[i];
			if (code == Code('R', 'E', 'H', 'E') || code == Code('F', 'O', 'H', 'E')) tags |= 1u << 0;
			else if (code == Code('F', 'O', 'M', 'A') || code == Code('R', 'E', 'M', 'A')) tags |= 1u << 1;
			else if (code == Code('R', 'E', 'F', 'A') || code == Code('F', 'O', 'F', 'A')) tags |= 1u << 2;
			else if (code == Code('C', 'U', 'D', 'I') || code == Code('C', 'U', 'P', 'O') || code == Code('C', 'U', 'P', 'A')) tags |= 1u << 3;
		}
		if (a_hostileCount > 0) tags |= 1u << 4;
		return tags;
	}

	// Magic rows (VLegacyMagicMenuItem): School byte 0 alteration .. 5 restoration; Category 4 spell, 8 power (proven 2026-10-24).
	// School choices: bits 0-5 the schools, bit 6 Powers (a power also keeps its school bit).
	constexpr std::uint32_t SchoolTags(unsigned a_school, unsigned a_category)
	{
		std::uint32_t tags = a_school <= 5 ? 1u << a_school : 0u;
		if (a_category == 8) tags |= 1u << 6;
		return tags;
	}

	// Effect-family membership comes from verified Remastered effect codes.
	constexpr std::uint32_t EffectTags(const std::uint32_t* a_codes, unsigned a_count)
	{
		return magiceffects::Tags(a_codes, a_count);
	}

	// Misc: form type (RE::FormType) + base form id + book flags -> Keys, Soul gems, Books/Scrolls, Tools, Other.
	// Lockpicks are the Misc form 0x0000000A. Alchemy apparatus and ingredients live on the potions page and have no misc tag.
	constexpr std::uint32_t MiscTags(unsigned a_formType, std::uint32_t a_formId, std::uint8_t)
	{
		switch (a_formType) {
		case 39: return 1u << 0;  // key
		case 38: return 1u << 1;  // soul gem
		case 21: return 1u << 2;  // book or scroll
		case 27: return (a_formId & 0x00FFFFFFu) == 0x0Au ? (1u << 3) : (1u << 4);
		case 26: case 42: return 1u << 4;  // light, sigil stone
		default: return 0;
		}
	}
}
