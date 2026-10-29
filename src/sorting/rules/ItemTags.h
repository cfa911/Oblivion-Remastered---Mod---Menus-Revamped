#pragma once

#include "sorting/rules/GearPieces.h"
#include "sorting/rules/MagicEffects.h"
#include "sorting/rules/PickerGrid.h"
#include "sorting/rules/WeaponKinds.h"

#include <array>
#include <cstdint>
#include <string_view>

// Per-tab picker choices and the pure classifiers that turn a native item into "tag" bits.
// A tag bit k means "belongs to choice k + 1 of the item's own tab"; choice 0 is always All.
// Page numbers (CurrentPageInventory, proven in game 1.512.105.0): 0 weapons, 1 gear, 2 potions, 3 misc, 4 all items.
namespace itemtags
{
	enum class Tab : unsigned { Weapons, Gear, Potions, Misc, AllItems, MagicSchool, MagicEffect, PotionEffects };
	// Flat tag/selection IDs keep parent navigation separate from applied membership.
	// Potions is the native page-2 enum; its popup root is now Alchemy.
	enum class Alchemy : unsigned { All, Potions, Ingredients, Tools, Health, Magicka, Fatigue, Attributes, Cure, Poison };
	constexpr unsigned Index(Alchemy choice) { return static_cast<unsigned>(choice); }
	constexpr std::uint32_t AlchemyBit(Alchemy choice)
	{
		const auto index = Index(choice);
		return index > 0 && index <= Index(Alchemy::Poison) ? 1u << (index - 1) : 0u;
	}
	constexpr std::uint32_t AlchemyTags(unsigned formType)
	{
		return formType == 40 ? AlchemyBit(Alchemy::Potions) :
			formType == 25 ? AlchemyBit(Alchemy::Ingredients) :
			formType == 19 ? AlchemyBit(Alchemy::Tools) : 0u;
	}
	// Potion submenu: All potions=1; Health..Poison are flat IDs 4..9.
	constexpr unsigned PotionSelection(unsigned choice) { return choice == 0 ? Index(Alchemy::Potions) : choice + 3; }
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
		const wchar_t* icons[kMaxChoices];  // PNG filenames in OBSE/Plugins/MenusRevamped/Sorting/
	};

	inline const TabInfo& Info(Tab a_tab)
	{
		static const TabInfo tabs[] = {
			{ "Weapon type", weaponkinds::kCount,
				{ "All", "Daggers", "Shortswords", "Longswords", "Claymores", "War axes", "Battleaxes", "Maces", "Warhammers", "Bows", "Arrows", "Staves", "Other weapons" },
				{ L"Weapon-All.png", L"Weapon-Dagger.png", L"Weapon-Shortsword.png", L"Weapon-Longsword.png", L"Weapon-Claymore.png", L"Weapon-WarAxe.png", L"Weapon-Battleaxe.png", L"Weapon-Mace.png", L"Weapon-Warhammer.png", L"Weapon-Bow.png", L"Weapon-Arrow.png", L"Weapon-Staff.png", L"Weapon-Other.png" } },
			{ "Equipment piece", 9, { "All", "Ring", "Amulet", "Head", "Torso", "Arms", "Feet", "Legwear", "Shield" },
				{ L"Gear-All.png", L"Gear-Ring.png", L"Gear-Amulet.png", L"Gear-Head.png", L"Gear-Torso.png", L"Gear-Arms.png", L"Gear-Feet.png", L"Gear-Legwear.png", L"Gear-Shield.png" } },
			{ "Alchemy", 4, { "All", "Potions", "Ingredients", "Tools" },
				{ L"Alchemy-All.png", L"Alchemy-Potions.png", L"Alchemy-Ingredients.png", L"Alchemy-Tools.png" } },
			{ "Item type", 6, { "All", "Keys", "Soul gems", "Books", "Tools", "Other" },
				{ L"Misc-All.png", L"Misc-Keys.png", L"Misc-SoulGems.png", L"Misc-Books.png", L"Misc-Tools.png", L"Misc-Other.png" } },
			// All Items: the four tab categories first; a category then opens its own choices.
			{ "Item category", 5, { "All", "Weapons", "Gear", "Alchemy", "Misc" },
				{ L"Category-All.png", L"Category-Weapons.png", L"Category-Gear.png", L"Category-Alchemy.png", L"Category-Misc.png" } },
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
			{ "Potion type", 7, { "All potions", "Health", "Magicka", "Fatigue", "Attributes", "Cure", "Poison" },
				{ L"Potion-All.png", L"Potion-Health.png", L"Potion-Magicka.png", L"Potion-Fatigue.png", L"Potion-Attributes.png", L"Potion-Cure.png", L"Potion-Poison.png" } },
		};
		return tabs[static_cast<unsigned>(a_tab)];
	}

	inline std::string_view PopupLabel(Tab tab, unsigned choice)
	{
		const auto& info = Info(tab);
		if (choice >= info.count) return {};
		if (tab == Tab::MagicSchool) return SchoolPopupLabel(choice);
		if (tab == Tab::MagicEffect) return magiceffects::kChoices[choice].label;
		return info.names[choice];
	}

	inline const wchar_t* SelectionIcon(Tab tab, unsigned choice)
	{
		if (tab == Tab::Potions && choice == Index(Alchemy::Potions))
			return Info(Tab::PotionEffects).icons[0];
		if (tab == Tab::Potions && choice >= Index(Alchemy::Health) && choice <= Index(Alchemy::Poison))
			return Info(Tab::PotionEffects).icons[choice - 3];
		const auto& info = Info(tab);
		return info.icons[choice < info.count ? choice : 0];
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
		case 33: case 34: return 1;                 // weapon, ammo
		case 20: case 22: return 2;                 // armor, clothing
		case 40: case 25: case 19: return 3;        // alchemy, ingredient, apparatus
		case 27: case 26: case 42: case 38: case 21: case 39: return 4;  // misc, light, sigil stone, soul gem, book, key
		default: return 0;
		}
	}
	constexpr unsigned CategoryOf(std::uint32_t a_tags) { return a_tags >> 28; }
	constexpr bool Includes(unsigned a_choice, std::uint32_t a_tags) { return a_choice == 0 || (a_choice <= 28 && ((a_tags & kTagBits) >> (a_choice - 1) & 1u) != 0); }
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

	// AMMO is a separate native form, not a WEAP type or a filename hint.
	constexpr std::uint32_t AmmoTags(unsigned formType)
	{
		return formType == 34 ? weaponkinds::Bit(weaponkinds::Kind::Arrow) : 0u;
	}

	// Native type plus nonlocalized asset filenames; ambiguous/custom assets stay Other.
	constexpr std::uint32_t WeaponTags(int a_type, std::string_view a_model = {}, std::string_view a_icon = {})
	{
		return weaponkinds::Tags(a_type, a_model, a_icon);
	}

	constexpr std::uint32_t Code(char a, char b, char c, char d)
	{
		return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) | (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8) |
		       (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
	}

	// Potion records always belong to Potions, including unclassified/scripted effects.
	// Families are independent of Ingredients and alchemy apparatus/Tools.
	// A potion with several kinds of effect belongs to every matching choice; Poison means "has a hostile effect".
	constexpr std::uint32_t PotionTags(const std::uint32_t* a_codes, unsigned a_count, int a_hostileCount)
	{
		std::uint32_t tags = AlchemyBit(Alchemy::Potions);
		for (unsigned i = 0; i < a_count; ++i) {
			const auto code = a_codes[i];
			if (code == Code('R', 'E', 'H', 'E') || code == Code('F', 'O', 'H', 'E')) tags |= AlchemyBit(Alchemy::Health);
			else if (code == Code('F', 'O', 'S', 'P') || code == Code('R', 'E', 'S', 'P')) tags |= AlchemyBit(Alchemy::Magicka);
			else if (code == Code('R', 'E', 'F', 'A') || code == Code('F', 'O', 'F', 'A')) tags |= AlchemyBit(Alchemy::Fatigue);
			else if (code == Code('C', 'U', 'D', 'I') || code == Code('C', 'U', 'P', 'O') || code == Code('C', 'U', 'P', 'A')) tags |= AlchemyBit(Alchemy::Cure);
			else if (code == Code('F', 'O', 'A', 'T') || code == Code('R', 'E', 'A', 'T') ||
				code == Code('D', 'G', 'A', 'T') || code == Code('D', 'R', 'A', 'T') || code == Code('A', 'B', 'A', 'T')) tags |= AlchemyBit(Alchemy::Attributes);
		}
		if (a_hostileCount > 0) tags |= AlchemyBit(Alchemy::Poison);
		return tags;
	}

	// Native RE::MagicSystem::SpellType, NOT the unrelated UI Category byte.
	// CommonLib enum + captured native spell list: 0 spell, 2 power, 3 lesser power.
	constexpr bool IsPowerSpell(unsigned a_type) { return a_type == 2 || a_type == 3; }
	constexpr bool IsCastableSpell(unsigned a_type) { return a_type == 0 || IsPowerSpell(a_type); }
	// School byte comes from the row. Both native power types keep that school and the Powers bit.
	constexpr std::uint32_t SchoolTags(unsigned a_school, unsigned a_spellType)
	{
		if (!IsCastableSpell(a_spellType)) return 0;
		std::uint32_t tags = a_school <= 5 ? 1u << a_school : 0u;
		if (IsPowerSpell(a_spellType)) tags |= 1u << 6;
		return tags;
	}

	// Effect-family membership comes from verified Remastered effect codes.
	constexpr std::uint32_t EffectTags(const std::uint32_t* a_codes, unsigned a_count)
	{
		return magiceffects::Tags(a_codes, a_count);
	}

	// Misc: form type (RE::FormType) + base form id + book flags -> Keys, Soul gems, Books/Scrolls, Tools, Other.
	// Installed master: Lockpick 0xA and RepairHammer 0xC. Inventory lights (including
	// torches) are Tools. Alchemy apparatus/ingredients have their own Alchemy groups.
	constexpr std::uint32_t MiscTags(unsigned a_formType, std::uint32_t a_formId, std::uint8_t)
	{
		switch (a_formType) {
		case 39: return 1u << 0;  // key
		case 38: return 1u << 1;  // soul gem
		case 21: return 1u << 2;  // book or scroll
		case 27: return ((a_formId & 0x00FFFFFFu) == 0x0Au || a_formId == 0x0Cu) ? (1u << 3) : (1u << 4);
		case 26: return 1u << 3;  // inventory light/torch
		case 42: return 1u << 4;  // sigil stone
		default: return 0;
		}
	}
}
