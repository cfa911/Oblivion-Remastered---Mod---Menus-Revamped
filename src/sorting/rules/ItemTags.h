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
	enum class Tab : unsigned { Weapons, Gear, Potions, Misc, AllItems, MagicSchool, MagicEffect, PotionEffects, MagicType, PoisonEffects };
	// Flat tag/selection IDs keep parent navigation separate from applied membership.
	// Potions is the native page-2 enum; its popup root is now Alchemy (All, Potions, Poisons, Ingredients, Tools).
	// Potions and Poisons each open their own group submenu (owner regrouping 2026-10-30).
	enum class Alchemy : unsigned {
		All, Potions, Poisons, Ingredients, Tools,
		Restore, Fortify, Ward, Stealth, Utility, Cures, Other,
		Damage, Elemental, Disable
	};
	constexpr unsigned Index(Alchemy choice) { return static_cast<unsigned>(choice); }
	constexpr std::uint32_t AlchemyBit(Alchemy choice)
	{
		const auto index = Index(choice);
		return index > 0 && index <= Index(Alchemy::Disable) ? 1u << (index - 1) : 0u;
	}
	constexpr std::uint32_t AlchemyTags(unsigned formType)
	{
		return formType == 40 ? AlchemyBit(Alchemy::Potions) :
			formType == 25 ? AlchemyBit(Alchemy::Ingredients) :
			formType == 19 ? AlchemyBit(Alchemy::Tools) : 0u;
	}
	// Potion submenu: All potions=Potions; Restore..Other follow. Poison submenu: All poisons=Poisons; Damage..Disable.
	constexpr unsigned PotionSelection(unsigned choice) { return choice == 0 ? Index(Alchemy::Potions) : Index(Alchemy::Restore) + choice - 1; }
	constexpr unsigned PoisonSelection(unsigned choice) { return choice == 0 ? Index(Alchemy::Poisons) : Index(Alchemy::Damage) + choice - 1; }
	constexpr bool IsPotionGroup(unsigned id) { return id >= Index(Alchemy::Restore) && id <= Index(Alchemy::Other); }
	constexpr bool IsPoisonGroup(unsigned id) { return id >= Index(Alchemy::Damage) && id <= Index(Alchemy::Disable); }
	constexpr unsigned kMaxChoices = pickergrid::kMaxChoices;
	static_assert(magiceffects::kChoiceCount <= kMaxChoices);

	// Only the Magic School popup uses these short display labels. TabInfo names stay
	// complete for tooltips, selection identity, and all other pickers.
	inline constexpr std::array<std::string_view, 7> kSchoolPopupLabels{
		"All", "Alte", "Conj", "Dest", "Illu", "Myst", "Rest"
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
			{ "Alchemy", 5, { "All", "Potions", "Poisons", "Ingredients", "Tools" },
				{ L"Alchemy-All.png", L"Alchemy-Potions.png", L"Alchemy-Poisons.png", L"Alchemy-Ingredients.png", L"Alchemy-Tools.png" } },
			{ "Item type", 6, { "All", "Keys", "Soul gems", "Books", "Tools", "Other" },
				{ L"Misc-All.png", L"Misc-Keys.png", L"Misc-SoulGems.png", L"Misc-Books.png", L"Misc-Tools.png", L"Misc-Other.png" } },
			// All Items: the four tab categories first; a category then opens its own choices.
			{ "Item category", 5, { "All", "Weapons", "Gear", "Alchemy", "Misc" },
				{ L"Category-All.png", L"Category-Weapons.png", L"Category-Gear.png", L"Category-Alchemy.png", L"Category-Misc.png" } },
		// Magic menu: independent school, effect family and source-type pickers.
			{ "Spell school", 7, { "All", "Alteration", "Conjuration", "Destruction", "Illusion", "Mysticism", "Restoration" },
				{ L"School-All.png", L"School-Alteration.png", L"School-Conjuration.png", L"School-Destruction.png", L"School-Illusion.png", L"School-Mysticism.png", L"School-Restoration.png" } },
			[] {
				TabInfo info{ "Spell effect", magiceffects::kChoiceCount, {}, {} };
				for (unsigned i = 0; i < info.count; ++i) {
					info.names[i] = magiceffects::kChoices[i].name;
					info.icons[i] = magiceffects::kChoices[i].icon;
				}
				return info;
			}(),
			// Every potion/poison group has its own editable file (owner 2026-10-30).
			{ "Potion type", 8, { "All potions", "Restore", "Fortify", "Ward", "Stealth", "Utility", "Cures", "Other" },
				{ L"Potion-All.png", L"Potion-Restore.png", L"Potion-Fortify.png", L"Potion-Ward.png", L"Potion-Stealth.png", L"Potion-Utility.png", L"Potion-Cure.png", L"Potion-Other.png" } },
			{ "Magic type", 4, { "All", "Spells", "Scrolls", "Powers" },
			{ L"MagicType-All.png", L"MagicType-Spells.png", L"MagicType-Scrolls.png", L"School-Powers.png" } },
			{ "Poison type", 4, { "All poisons", "Damage", "Elemental", "Disable" },
				{ L"Poison-All.png", L"Poison-Damage.png", L"Poison-Elemental.png", L"Poison-Disable.png" } },
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
		if (tab == Tab::Potions && IsPotionGroup(choice))
			return Info(Tab::PotionEffects).icons[choice - Index(Alchemy::Restore) + 1];
		if (tab == Tab::Potions && choice == Index(Alchemy::Poisons))
			return Info(Tab::PoisonEffects).icons[0];
		if (tab == Tab::Potions && IsPoisonGroup(choice))
			return Info(Tab::PoisonEffects).icons[choice - Index(Alchemy::Damage) + 1];
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

	// Effect code -> potion or poison group. Every positive alchemy effect (UESP Oblivion:Alchemy_Effects) has a
	// potion group; every all-hostile master poison has a poison group (coverage: docs/inventory/alchemy-misc).
	struct EffectGroup { std::uint32_t code; Alchemy group; };
	inline constexpr EffectGroup kAlchemyGroups[]{
		{ Code('R', 'E', 'H', 'E'), Alchemy::Restore }, { Code('R', 'E', 'S', 'P'), Alchemy::Restore },
		{ Code('R', 'E', 'F', 'A'), Alchemy::Restore }, { Code('R', 'E', 'A', 'T'), Alchemy::Restore },
		{ Code('F', 'O', 'H', 'E'), Alchemy::Fortify }, { Code('F', 'O', 'S', 'P'), Alchemy::Fortify }, { Code('F', 'O', 'F', 'A'), Alchemy::Fortify },
		{ Code('F', 'O', 'A', 'T'), Alchemy::Fortify }, { Code('F', 'O', 'S', 'K'), Alchemy::Fortify }, { Code('F', 'O', 'M', 'M'), Alchemy::Fortify },
		{ Code('R', 'S', 'D', 'I'), Alchemy::Ward }, { Code('R', 'S', 'F', 'I'), Alchemy::Ward }, { Code('R', 'S', 'F', 'R'), Alchemy::Ward },
		{ Code('R', 'S', 'M', 'A'), Alchemy::Ward }, { Code('R', 'S', 'N', 'W'), Alchemy::Ward }, { Code('R', 'S', 'P', 'A'), Alchemy::Ward },
		{ Code('R', 'S', 'P', 'O'), Alchemy::Ward }, { Code('R', 'S', 'S', 'H'), Alchemy::Ward }, { Code('S', 'H', 'L', 'D'), Alchemy::Ward },
		{ Code('F', 'I', 'S', 'H'), Alchemy::Ward }, { Code('F', 'R', 'S', 'H'), Alchemy::Ward }, { Code('L', 'I', 'S', 'H'), Alchemy::Ward },
		{ Code('R', 'E', 'D', 'G'), Alchemy::Ward }, { Code('R', 'F', 'L', 'C'), Alchemy::Ward }, { Code('S', 'A', 'B', 'S'), Alchemy::Ward },
		{ Code('C', 'H', 'M', 'L'), Alchemy::Stealth }, { Code('I', 'N', 'V', 'I'), Alchemy::Stealth },
		{ Code('N', 'E', 'Y', 'E'), Alchemy::Utility }, { Code('L', 'G', 'H', 'T'), Alchemy::Utility }, { Code('D', 'T', 'C', 'T'), Alchemy::Utility },
		{ Code('F', 'T', 'H', 'R'), Alchemy::Utility }, { Code('W', 'A', 'B', 'R'), Alchemy::Utility }, { Code('W', 'A', 'W', 'A'), Alchemy::Utility },
		{ Code('C', 'U', 'D', 'I'), Alchemy::Cures }, { Code('C', 'U', 'P', 'O'), Alchemy::Cures },
		{ Code('C', 'U', 'P', 'A'), Alchemy::Cures }, { Code('D', 'S', 'P', 'L'), Alchemy::Cures },
		{ Code('D', 'G', 'H', 'E'), Alchemy::Damage }, { Code('D', 'G', 'S', 'P'), Alchemy::Damage }, { Code('D', 'G', 'F', 'A'), Alchemy::Damage },
		{ Code('D', 'G', 'A', 'T'), Alchemy::Damage }, { Code('D', 'R', 'H', 'E'), Alchemy::Damage }, { Code('D', 'R', 'S', 'P'), Alchemy::Damage },
		{ Code('D', 'R', 'F', 'A'), Alchemy::Damage }, { Code('D', 'R', 'A', 'T'), Alchemy::Damage }, { Code('D', 'R', 'S', 'K'), Alchemy::Damage },
		{ Code('F', 'I', 'D', 'G'), Alchemy::Elemental }, { Code('F', 'R', 'D', 'G'), Alchemy::Elemental }, { Code('S', 'H', 'D', 'G'), Alchemy::Elemental },
		{ Code('W', 'K', 'F', 'I'), Alchemy::Elemental }, { Code('W', 'K', 'F', 'R'), Alchemy::Elemental }, { Code('W', 'K', 'S', 'H'), Alchemy::Elemental },
		{ Code('P', 'A', 'R', 'A'), Alchemy::Disable }, { Code('S', 'L', 'N', 'C'), Alchemy::Disable }, { Code('B', 'R', 'D', 'N'), Alchemy::Disable },
	};

	// Poison = native poison spell type, or every effect hostile (same rule as the wheel; owner RMI-014).
	// A poison carries Poisons plus its poison groups; a potion carries Potions plus every matching potion group
	// (hostile side effects of a mixed potion add nothing), or Other when none matches.
	constexpr bool IsPoison(unsigned a_count, int a_hostileCount, bool a_poisonType = false)
	{
		return a_poisonType || (a_count > 0 && a_hostileCount >= static_cast<int>(a_count));
	}
	constexpr std::uint32_t PotionTags(const std::uint32_t* a_codes, unsigned a_count, int a_hostileCount, bool a_poisonType = false)
	{
		const bool poison = IsPoison(a_count, a_hostileCount, a_poisonType);
		std::uint32_t groups = 0;
		for (unsigned i = 0; i < a_count; ++i)
			for (const auto& entry : kAlchemyGroups)
				if (entry.code == a_codes[i] && IsPoisonGroup(Index(entry.group)) == poison) groups |= AlchemyBit(entry.group);
		if (poison) return AlchemyBit(Alchemy::Poisons) | groups;
		return AlchemyBit(Alchemy::Potions) | (groups ? groups : AlchemyBit(Alchemy::Other));
	}

	// Native RE::MagicSystem::SpellType, NOT the unrelated UI Category byte.
	// CommonLib enum + captured native spell list: 0 spell, 2 power, 3 lesser power.
	constexpr bool IsPowerSpell(unsigned a_type) { return a_type == 2 || a_type == 3; }
	constexpr bool IsCastableSpell(unsigned a_type) { return a_type == 0 || IsPowerSpell(a_type); }
	// School is independent of spell/scroll/power type; powers retain their own school.
	constexpr std::uint32_t SchoolTags(unsigned a_school, unsigned a_spellType)
	{
		if (!IsCastableSpell(a_spellType)) return 0;
		return a_school <= 5 ? 1u << a_school : 0u;
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
