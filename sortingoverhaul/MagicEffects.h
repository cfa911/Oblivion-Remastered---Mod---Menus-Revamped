#pragma once
#include <array>
#include <cstdint>
#include <string_view>

// Codes verified from the installed Remastered Oblivion.esm MGEF records, 2026-10-25.
// See docs/spell-effect-categories.md and tests/remastered_effect_fixtures.h.
// Runtime classification is code-based, never spell-name or school based.
namespace magiceffects
{
	enum class Choice : unsigned { All, Fire, Frost, Shock, Damage, Drain, Absorb, Restore, Fortify, Shield, Resistance, Weakness, Summon, Bound, Control, Stealth, Utility, Other, Count };
	constexpr unsigned kChoiceCount = static_cast<unsigned>(Choice::Count);
	struct ChoiceInfo { const char* name; const char* label; const wchar_t* icon; };
	inline constexpr std::array<ChoiceInfo, kChoiceCount> kChoices{{
		{ "All", "All", L"Effect-All.png" },
		{ "Fire", "Fire", L"Effect-Fire.png" },
		{ "Frost", "Frost", L"Effect-Frost.png" },
		{ "Shock", "Shock", L"Effect-Shock.png" },
		{ "Damage", "Damage", L"Effect-Damage.png" },
		{ "Drain", "Drain", L"Effect-Drain.png" },
		{ "Absorb", "Absorb", L"Effect-Absorb.png" },
		{ "Restore & Cure", "Restore", L"Effect-Restore.png" },
		{ "Fortify", "Fortify", L"Effect-Fortify.png" },
		{ "Shield", "Shield", L"Effect-Shield.png" },
		{ "Resistance", "Resist", L"Effect-Resistance.png" },
		{ "Weakness", "Weak", L"Effect-Weakness.png" },
		{ "Summon", "Summon", L"Effect-Summon.png" },
		{ "Bound Equipment", "Bound", L"Effect-Bound.png" },
		{ "Control", "Control", L"Effect-Control.png" },
		{ "Stealth", "Stealth", L"Effect-Stealth.png" },
		{ "Utility", "Utility", L"Effect-Utility.png" },
		{ "Other", "Other", L"Effect-Other.png" },
	}};

	constexpr std::uint32_t Code(std::string_view value)
	{
		return value.size() == 4 ? static_cast<std::uint32_t>(static_cast<unsigned char>(value[0])) |
			(static_cast<std::uint32_t>(static_cast<unsigned char>(value[1])) << 8) |
			(static_cast<std::uint32_t>(static_cast<unsigned char>(value[2])) << 16) |
			(static_cast<std::uint32_t>(static_cast<unsigned char>(value[3])) << 24) : 0u;
	}
	constexpr std::uint32_t Bit(Choice choice)
	{
		const auto index = static_cast<unsigned>(choice);
		return index > 0 && index < kChoiceCount ? 1u << (index - 1) : 0u;
	}
	struct Rule { std::uint32_t code; Choice choice; };
	inline constexpr auto kRules = std::to_array<Rule>({
		// Fire
		{ Code("FIDG"), Choice::Fire }, // Fire Damage
		// Frost
		{ Code("FRDG"), Choice::Frost }, // Frost Damage
		// Shock
		{ Code("SHDG"), Choice::Shock }, // Shock Damage
		// Damage
		{ Code("DGAT"), Choice::Damage }, // Damage Attribute
		{ Code("DGFA"), Choice::Damage }, // Damage Fatigue
		{ Code("DGHE"), Choice::Damage }, // Damage Health
		{ Code("DGSP"), Choice::Damage }, // Damage Magicka
		{ Code("DIAR"), Choice::Damage }, // Disintegrate Armor
		{ Code("DIWE"), Choice::Damage }, // Disintegrate Weapon
		{ Code("SUDG"), Choice::Damage }, // Sun Damage
		// Drain
		{ Code("DRAT"), Choice::Drain }, // Drain Attribute
		{ Code("DRFA"), Choice::Drain }, // Drain Fatigue
		{ Code("DRHE"), Choice::Drain }, // Drain Health
		{ Code("DRSK"), Choice::Drain }, // Drain Skill
		{ Code("DRSP"), Choice::Drain }, // Drain Magicka
		// Absorb
		{ Code("ABAT"), Choice::Absorb }, // Absorb Attribute
		{ Code("ABFA"), Choice::Absorb }, // Absorb Fatigue
		{ Code("ABHE"), Choice::Absorb }, // Absorb Health
		{ Code("ABSK"), Choice::Absorb }, // Absorb Skill
		{ Code("ABSP"), Choice::Absorb }, // Absorb Magicka
		// Restore
		{ Code("CUDI"), Choice::Restore }, // Cure Disease
		{ Code("CUPA"), Choice::Restore }, // Cure Paralysis
		{ Code("CUPO"), Choice::Restore }, // Cure Poison
		{ Code("REAT"), Choice::Restore }, // Restore Attribute
		{ Code("REFA"), Choice::Restore }, // Restore Fatigue
		{ Code("REHE"), Choice::Restore }, // Restore Health
		{ Code("RESP"), Choice::Restore }, // Restore Magicka
		// Fortify
		{ Code("FOAT"), Choice::Fortify }, // Fortify Attribute
		{ Code("FOFA"), Choice::Fortify }, // Fortify Fatigue
		{ Code("FOHE"), Choice::Fortify }, // Fortify Health
		{ Code("FOMM"), Choice::Fortify }, // Fortify Magicka Multiplier
		{ Code("FOSK"), Choice::Fortify }, // Fortify Skill
		{ Code("FOSP"), Choice::Fortify }, // Fortify Magicka
		// Shield
		{ Code("FISH"), Choice::Shield }, // Fire Shield
		{ Code("FRSH"), Choice::Shield }, // Frost Shield
		{ Code("LISH"), Choice::Shield }, // Shock Shield
		{ Code("SHLD"), Choice::Shield }, // Shield
		// Resistance
		{ Code("RSDI"), Choice::Resistance }, // Resist Disease
		{ Code("RSFI"), Choice::Resistance }, // Resist Fire
		{ Code("RSFR"), Choice::Resistance }, // Resist Frost
		{ Code("RSMA"), Choice::Resistance }, // Resist Magic
		{ Code("RSNW"), Choice::Resistance }, // Resist Normal Weapons
		{ Code("RSPA"), Choice::Resistance }, // Resist Paralysis
		{ Code("RSPO"), Choice::Resistance }, // Resist Poison
		{ Code("RSSH"), Choice::Resistance }, // Resist Shock
		// Weakness
		{ Code("WKDI"), Choice::Weakness }, // Weakness to Disease
		{ Code("WKFI"), Choice::Weakness }, // Weakness to Fire
		{ Code("WKFR"), Choice::Weakness }, // Weakness to Frost
		{ Code("WKMA"), Choice::Weakness }, // Weakness to Magic
		{ Code("WKNW"), Choice::Weakness }, // Weakness to Normal Weapons
		{ Code("WKPO"), Choice::Weakness }, // Weakness to Poison
		{ Code("WKSH"), Choice::Weakness }, // Weakness to Shock
		// Summon
		{ Code("REAN"), Choice::Summon }, // Reanimate
		{ Code("Z001"), Choice::Summon }, // Summon Rufio's Ghost
		{ Code("Z002"), Choice::Summon }, // Summon Ancestor Guardian
		{ Code("Z003"), Choice::Summon }, // Summon Spiderling
		{ Code("Z004"), Choice::Summon }, // Summon Flesh Atronach
		{ Code("Z005"), Choice::Summon }, // Summon Bear
		{ Code("Z006"), Choice::Summon }, // Summon Gluttonous Hunger
		{ Code("Z007"), Choice::Summon }, // Summon Ravenous Hunger
		{ Code("Z008"), Choice::Summon }, // Summon Voracious Hunger
		{ Code("Z009"), Choice::Summon }, // Summon Dark Seducer
		{ Code("Z010"), Choice::Summon }, // Summon Golden Saint
		{ Code("Z011"), Choice::Summon }, // Wabba Summon
		{ Code("Z012"), Choice::Summon }, // Summon Decrepit Shambles
		{ Code("Z013"), Choice::Summon }, // Summon Shambles
		{ Code("Z014"), Choice::Summon }, // Summon Replete Shambles
		{ Code("Z015"), Choice::Summon }, // Summon Hunger
		{ Code("Z016"), Choice::Summon }, // Summon Mangled Flesh Atronach
		{ Code("Z017"), Choice::Summon }, // Summon Torn Flesh Atronach
		{ Code("Z018"), Choice::Summon }, // Summon Stitched Flesh Atronach
		{ Code("Z019"), Choice::Summon }, // Summon Sewn Flesh Atronach
		{ Code("ZCLA"), Choice::Summon }, // Summon Clannfear
		{ Code("ZDAE"), Choice::Summon }, // Summon Daedroth
		{ Code("ZDRE"), Choice::Summon }, // Summon Dremora
		{ Code("ZDRL"), Choice::Summon }, // Summon Dremora Lord
		{ Code("ZFIA"), Choice::Summon }, // Summon Flame Atronach
		{ Code("ZFRA"), Choice::Summon }, // Summon Frost Atronach
		{ Code("ZGHO"), Choice::Summon }, // Summon Ghost
		{ Code("ZHDZ"), Choice::Summon }, // Summon Headless Zombie
		{ Code("ZLIC"), Choice::Summon }, // Summon Lich
		{ Code("ZSCA"), Choice::Summon }, // Summon Scamp
		{ Code("ZSKA"), Choice::Summon }, // Summon Skeleton Guardian
		{ Code("ZSKC"), Choice::Summon }, // Summon Skeleton Champion
		{ Code("ZSKE"), Choice::Summon }, // Summon Skeleton
		{ Code("ZSKH"), Choice::Summon }, // Summon Skeleton Hero
		{ Code("ZSPD"), Choice::Summon }, // Summon Spider Daedra
		{ Code("ZSTA"), Choice::Summon }, // Summon Storm Atronach
		{ Code("ZWRA"), Choice::Summon }, // Summon Faded Wraith
		{ Code("ZWRL"), Choice::Summon }, // Summon Gloom Wraith
		{ Code("ZXIV"), Choice::Summon }, // Summon Xivilai
		{ Code("ZZOM"), Choice::Summon }, // Summon Zombie
		// Bound
		{ Code("MYHL"), Choice::Bound }, // Summon Mythic Dawn Helm
		{ Code("MYTH"), Choice::Bound }, // Summon Mythic Dawn Armor
		{ Code("BABO"), Choice::Bound }, // Bound Boots
		{ Code("BACU"), Choice::Bound }, // Bound Cuirass
		{ Code("BAGA"), Choice::Bound }, // Bound Gauntlets
		{ Code("BAGR"), Choice::Bound }, // Bound Greaves
		{ Code("BAHE"), Choice::Bound }, // Bound Helmet
		{ Code("BASH"), Choice::Bound }, // Bound Shield
		{ Code("BW01"), Choice::Bound }, // Bound Order Weapon 1
		{ Code("BW02"), Choice::Bound }, // Bound Order Weapon 2
		{ Code("BW03"), Choice::Bound }, // Bound Order Weapon 3
		{ Code("BW04"), Choice::Bound }, // Bound Order Weapon 4
		{ Code("BW05"), Choice::Bound }, // Bound Order Weapon 5
		{ Code("BW06"), Choice::Bound }, // Bound Order Weapon 6
		{ Code("BW07"), Choice::Bound }, // Summon Staff of Sheogorath
		{ Code("BW08"), Choice::Bound }, // Bound Priest Dagger
		{ Code("BWAX"), Choice::Bound }, // Bound Axe
		{ Code("BWBO"), Choice::Bound }, // Bound Bow
		{ Code("BWDA"), Choice::Bound }, // Bound Dagger
		{ Code("BWMA"), Choice::Bound }, // Bound Mace
		{ Code("BWSW"), Choice::Bound }, // Bound Sword
		// Control
		{ Code("CALM"), Choice::Control }, // Calm
		{ Code("CHRM"), Choice::Control }, // Charm
		{ Code("COCR"), Choice::Control }, // Command Creature
		{ Code("COHU"), Choice::Control }, // Command Humanoid
		{ Code("DEMO"), Choice::Control }, // Demoralize
		{ Code("FRNZ"), Choice::Control }, // Frenzy
		{ Code("PARA"), Choice::Control }, // Paralyze
		{ Code("RALY"), Choice::Control }, // Rally
		{ Code("SLNC"), Choice::Control }, // Silence
		{ Code("TURN"), Choice::Control }, // Turn Undead
		// Stealth
		{ Code("CHML"), Choice::Stealth }, // Chameleon
		{ Code("INVI"), Choice::Stealth }, // Invisibility
		// Utility
		{ Code("BRDN"), Choice::Utility }, // Burden
		{ Code("DSPL"), Choice::Utility }, // Dispel
		{ Code("DTCT"), Choice::Utility }, // Detect Life
		{ Code("FTHR"), Choice::Utility }, // Feather
		{ Code("LGHT"), Choice::Utility }, // Light
		{ Code("NEYE"), Choice::Utility }, // Night-Eye
		{ Code("OPEN"), Choice::Utility }, // Open
		{ Code("REDG"), Choice::Utility }, // Reflect Damage
		{ Code("RFLC"), Choice::Utility }, // Reflect Spell
		{ Code("SABS"), Choice::Utility }, // Spell Absorption
		{ Code("STRP"), Choice::Utility }, // Soul Trap
		{ Code("TELE"), Choice::Utility }, // Telekinesis
		{ Code("WABR"), Choice::Utility }, // Water Breathing
		{ Code("WAWA"), Choice::Utility }, // Water Walking
	});
	constexpr bool ValidRules()
	{
		for (unsigned i = 0; i < kRules.size(); ++i) {
			if (!kRules[i].code || !Bit(kRules[i].choice) || kRules[i].choice == Choice::Other) return false;
			for (unsigned j = 0; j < i; ++j) if (kRules[i].code == kRules[j].code) return false;
		}
		return true;
	}
	static_assert(ValidRules());
	static_assert(kChoiceCount == 18 && Bit(Choice::Other) == (1u << 16));

	constexpr Choice Classify(std::uint32_t code)
	{
		for (const auto& rule : kRules) if (rule.code == code) return rule.choice;
		return Choice::Other;
	}
	constexpr std::uint32_t Tags(const std::uint32_t* codes, unsigned count)
	{
		std::uint32_t tags = 0;
		if (codes) for (unsigned i = 0; i < count; ++i) {
			const auto choice = Classify(codes[i]);
			if (choice != Choice::Other) tags |= Bit(choice);
		}
		// Other is a whole-spell fallback, not a second tag on every mixed/scripted spell.
		return tags ? tags : Bit(Choice::Other);
	}
}
