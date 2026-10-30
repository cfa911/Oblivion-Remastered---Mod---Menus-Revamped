#include "../src/sorting/rules/GearPieces.h"
#include "../src/sorting/rules/ItemTags.h"

#include <array>
#include <cstdio>
#include <cstdlib>

int main()
{
	using gearpieces::Piece;
	int checks = 0;
	const auto check = [&](bool value, const char* label) {
		++checks;
		if (!value) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); }
	};
	// Native masks from the successful 30-row Gear capture, including custom equipment and a robe.
	constexpr std::array<std::uint16_t, 30> fixture = {
		4, 4, 4, 32, 3, 8, 8, 256, 256, 256, 256, 256, 256, 64, 64,
		64, 64, 64, 64, 64, 64, 192, 128, 64, 64, 64, 12, 64, 8192, 16
	};
	constexpr unsigned expected[] = { 30, 14, 6, 1, 4, 1, 1, 3, 1 };
	for (unsigned piece = 0; piece < 9; ++piece) {
		unsigned count = 0;
		for (auto slots : fixture) if (gearpieces::Includes(static_cast<Piece>(piece), slots)) ++count;
		check(count == expected[piece], "captured Gear category count");
	}
	check(gearpieces::Includes(Piece::Ring, 0x40), "right ring");
	check(gearpieces::Includes(Piece::Ring, 0x80), "left ring");
	check(gearpieces::Includes(Piece::Ring, 0xC0), "both ring slots");
	check(gearpieces::Includes(Piece::Head, 1), "head only");
	check(gearpieces::Includes(Piece::Head, 2), "hair only");
	check(gearpieces::Includes(Piece::Torso, 12), "robe torso");
	check(gearpieces::Includes(Piece::Legwear, 12), "robe legwear");
	check(!gearpieces::Includes(Piece::Feet, 8), "legwear is not feet");
	check(!gearpieces::Includes(Piece::Legwear, 32), "feet are not legwear");
	check(gearpieces::Includes(Piece::All, 0), "All retains uncategorized gear");
	check(!gearpieces::Includes(Piece::Ring, 0), "no-slot object is not a ring");
	check(!gearpieces::Includes(Piece::Shield, 0x8000), "tail is not shield");
	// Tab -> page numbers proven in game on 2026-10-24; choice counts match the popups.
	itemtags::Tab tab{};
	check(itemtags::TabFromPage(0, tab) && tab == itemtags::Tab::Weapons, "page 0 is weapons");
	check(itemtags::TabFromPage(1, tab) && tab == itemtags::Tab::Gear, "page 1 is gear");
	check(itemtags::TabFromPage(2, tab) && tab == itemtags::Tab::Potions, "page 2 is potions");
	check(itemtags::TabFromPage(3, tab) && tab == itemtags::Tab::Misc, "page 3 is misc");
	check(itemtags::TabFromPage(4, tab) && tab == itemtags::Tab::AllItems && !itemtags::TabFromPage(5, tab) && !itemtags::TabFromPage(-1, tab), "page 4 is all items");
	check(itemtags::Info(itemtags::Tab::Weapons).count == weaponkinds::kCount && itemtags::Info(itemtags::Tab::Gear).count == 9 &&
		itemtags::Info(itemtags::Tab::Potions).count == 5 && itemtags::Info(itemtags::Tab::Misc).count == 6, "choice counts");
	// Gear tags still agree with the captured masks: Ring is choice 1, Shield choice 8.
	check(itemtags::Includes(1, itemtags::GearTags(0xC0)) && itemtags::Includes(7, itemtags::GearTags(12)) && !itemtags::Includes(6, itemtags::GearTags(8)), "gear tags");
	check(itemtags::Includes(0, 0), "All keeps untagged items");
	check(!itemtags::Includes(1, 0), "untagged item is not choice 1");
	// Detailed melee tags use model/icon hints within native skill/handedness boundaries.
	check(itemtags::Includes(1, itemtags::WeaponTags(0, "Dagger.NIF")) && itemtags::Includes(4, itemtags::WeaponTags(1, "Claymore.NIF")), "blade subtypes");
	check(itemtags::Includes(5, itemtags::WeaponTags(2, "WarAxe.NIF")) && itemtags::Includes(6, itemtags::WeaponTags(3, "Battleaxe.NIF")), "axe subtypes");
	check(itemtags::Includes(9, itemtags::WeaponTags(5)) && itemtags::Includes(static_cast<unsigned>(weaponkinds::Kind::Staff), itemtags::WeaponTags(4)), "bow and staff unchanged");
	check(!itemtags::Includes(2, itemtags::WeaponTags(0)) && itemtags::Includes(static_cast<unsigned>(weaponkinds::Kind::Other), itemtags::WeaponTags(-1)) && itemtags::Includes(static_cast<unsigned>(weaponkinds::Kind::Other), itemtags::WeaponTags(9)), "unknown weapon fallback");
	// Potion family IDs are flat applied tags, independent of the nested popup.
	using itemtags::Code;
	using itemtags::Alchemy;
	using itemtags::AlchemyBit;
	using itemtags::Index;
	const std::uint32_t rest[] = { Code('R', 'E', 'H', 'E') };
	const std::uint32_t mixed[] = { Code('R', 'E', 'H', 'E'), Code('D', 'G', 'F', 'A') };
	const std::uint32_t cure[] = { Code('C', 'U', 'P', 'O') };
	const std::uint32_t fatigue[] = { Code('R', 'E', 'F', 'A') };
	const std::uint32_t damage[] = { Code('D', 'G', 'H', 'E') };
	check(Code('R', 'S', 'M', 'A') == 0x414D5352u, "effect code byte order matches the dump");
	check(itemtags::Includes(Index(Alchemy::Restore), itemtags::PotionTags(rest, 1, 0)) && !itemtags::Includes(Index(Alchemy::Poisons), itemtags::PotionTags(rest, 1, 0)), "restore health");
	check(itemtags::Includes(Index(Alchemy::Restore), itemtags::PotionTags(mixed, 2, 1)) && !itemtags::Includes(Index(Alchemy::Poisons), itemtags::PotionTags(mixed, 2, 1)), "mixed potion is restore, not poison");
	check(itemtags::Includes(Index(Alchemy::Cures), itemtags::PotionTags(cure, 1, 0)) && itemtags::Includes(Index(Alchemy::Restore), itemtags::PotionTags(fatigue, 1, 0)), "cure and fatigue");
	check(itemtags::Includes(Index(Alchemy::Damage), itemtags::PotionTags(damage, 1, 2)) && !itemtags::Includes(Index(Alchemy::Restore), itemtags::PotionTags(damage, 1, 2)), "poison is damage, not restore");
	check(itemtags::PotionTags(nullptr, 0, 0) == (AlchemyBit(Alchemy::Potions) | AlchemyBit(Alchemy::Other)), "unclassified potion is Other");
	// Actual master ALCH records: 00098499 PotionRestoreMagicka and 00098477 PotionFortifyMagicka.
	const std::uint32_t potionRESP[] = { Code('R', 'E', 'S', 'P') };
	check(itemtags::PotionTags(potionRESP, 1, 0) == (AlchemyBit(Alchemy::Potions) | AlchemyBit(Alchemy::Restore)), "vanilla RESP potion is Restore");
	check(itemtags::IncludesAll(3, Index(Alchemy::Restore), itemtags::PotionTags(potionRESP, 1, 0) | (3u << 28)), "All Items Restore RESP");
	const std::uint32_t potionFOSP[] = { Code('F', 'O', 'S', 'P') };
	check(itemtags::PotionTags(potionFOSP, 1, 0) == (AlchemyBit(Alchemy::Potions) | AlchemyBit(Alchemy::Fortify)), "vanilla FOSP potion is Fortify");
	check(itemtags::IncludesAll(3, Index(Alchemy::Fortify), itemtags::PotionTags(potionFOSP, 1, 0) | (3u << 28)), "All Items Fortify FOSP");
	const std::uint32_t inventedMagicka[] = { Code('R', 'E', 'M', 'A'), Code('F', 'O', 'M', 'A') };
	check(itemtags::PotionTags(inventedMagicka, 2, 0) == (AlchemyBit(Alchemy::Potions) | AlchemyBit(Alchemy::Other)), "invented codes fall to Other");
	const std::uint32_t mixedMagicka[] = { Code('R', 'E', 'S', 'P'), Code('R', 'E', 'H', 'E'), Code('R', 'E', 'F', 'A'), Code('C', 'U', 'P', 'O') };
	const auto mixedTags = AlchemyBit(Alchemy::Potions) | AlchemyBit(Alchemy::Restore) | AlchemyBit(Alchemy::Cures);
	check(itemtags::PotionTags(mixedMagicka, 4, 0) == mixedTags, "mixed potion retains Restore and Cures");
	check(itemtags::PotionTags(mixedMagicka, 4, 1) == mixedTags, "mixed potion with one hostile effect is not a poison");
	check(!itemtags::IncludesAll(2, Index(Alchemy::Restore), itemtags::PotionTags(potionRESP, 1, 0) | (3u << 28)), "Restore cannot leak into Gear");
	// Misc: stable native item kinds and the independently verified base repair-hammer form.
	check(itemtags::Includes(1, itemtags::MiscTags(39, 0x123, 0)), "key");
	check(itemtags::Includes(2, itemtags::MiscTags(38, 0x2CF9F, 0)), "soul gem");
	check(itemtags::Includes(3, itemtags::MiscTags(21, 0x5, 1)), "book or scroll");
	check(itemtags::Includes(4, itemtags::MiscTags(27, 0x0A, 0)) && itemtags::Includes(4, itemtags::MiscTags(27, 0xFF00000A, 0)), "existing lockpick rule retained");
	check(itemtags::Includes(4, itemtags::MiscTags(27, 0x0C, 0)) && !itemtags::Includes(5, itemtags::MiscTags(27, 0x0C, 0)), "base repair hammer moves to Tools");
	check(itemtags::Includes(5, itemtags::MiscTags(27, 0x191, 0)) && !itemtags::Includes(4, itemtags::MiscTags(27, 0x191, 0)), "other misc");
	check(itemtags::Includes(4, itemtags::MiscTags(26, 0x2CF9F, 0)) && itemtags::Includes(5, itemtags::MiscTags(42, 0x41FD3, 0)), "torch Tools and sigil stone Other");
	check(itemtags::MiscTags(19, 1, 0) == 0 && itemtags::MiscTags(25, 1, 0) == 0, "alchemy tools and ingredients do not become Misc");
// All Items: category from form type, then the sub-choice inside it.
	check(itemtags::Info(itemtags::Tab::AllItems).count == 5, "category count");
	check(itemtags::Category(33) == 1 && itemtags::Category(20) == 2 && itemtags::Category(22) == 2 && itemtags::Category(40) == 3 &&
		itemtags::Category(25) == 3 && itemtags::Category(19) == 3 && itemtags::Category(39) == 4 && itemtags::Category(21) == 4 && itemtags::Category(34) == 1, "categories");
	const std::uint32_t ring = itemtags::GearTags(0x40) | (2u << 28), bow = itemtags::WeaponTags(5) | (1u << 28), key = itemtags::MiscTags(39, 1, 0) | (4u << 28), ammo = itemtags::AmmoTags(34) | (itemtags::Category(34) << 28);
	check(itemtags::IncludesAll(0, 0, ammo) && itemtags::IncludesAll(0, 9, bow), "no category keeps everything");
	check(itemtags::IncludesAll(2, 0, ring) && !itemtags::IncludesAll(2, 0, bow), "whole gear category");
	check(itemtags::IncludesAll(2, 1, ring) && !itemtags::IncludesAll(2, 2, ring), "ring inside gear");
	check(itemtags::IncludesAll(1, 9, bow) && !itemtags::IncludesAll(1, 9, key), "bow only inside weapons");
	check(itemtags::IncludesAll(4, 1, key) && !itemtags::IncludesAll(3, 1, key), "key only in misc");
	check(itemtags::IncludesAll(1, 0, ammo) && itemtags::Includes(1, ring), "category bits do not disturb choice bits");
	// Magic: school/power and effect tags.
	check(itemtags::Info(itemtags::Tab::MagicSchool).count == 7 && itemtags::Info(itemtags::Tab::MagicEffect).count == magiceffects::kChoiceCount, "magic choice counts");
	constexpr std::array<std::string_view, 7> schoolLabels{ "All", "Alte", "Conj", "Dest", "Illu", "Myst", "Rest" };
	for (unsigned i = 0; i < schoolLabels.size(); ++i) check(itemtags::SchoolPopupLabel(i) == schoolLabels[i], "exact school display label");
	check(itemtags::SchoolPopupLabel(7).empty(), "out-of-range school label is empty");
	check(std::string_view(itemtags::Info(itemtags::Tab::MagicSchool).names[1]) == "Alteration", "full school tooltip remains available");
	check(itemtags::Includes(3, itemtags::SchoolTags(2, 0)) && !itemtags::Includes(1, itemtags::SchoolTags(2, 0)), "destruction school");
	check(itemtags::Includes(6, itemtags::SchoolTags(5, 0)) && !itemtags::Includes(7, itemtags::SchoolTags(5, 0)), "restoration is not a power");
	check(!itemtags::Includes(7, itemtags::SchoolTags(5, 2)) && itemtags::Includes(6, itemtags::SchoolTags(5, 2)), "a power keeps its school");
	check(itemtags::SchoolTags(9, 0) == 0, "unknown school is untagged");
	using magiceffects::Choice;
	const std::uint32_t fire[] = { Code('F', 'I', 'D', 'G') }, magicka[] = { Code('R', 'E', 'S', 'P') };
	check(itemtags::EffectTags(fire, 1) == magiceffects::Bit(Choice::Fire), "fire damage only");
	check(itemtags::EffectTags(magicka, 1) == magiceffects::Bit(Choice::Restore), "verified Restore Magicka code");
	check(itemtags::Includes(static_cast<unsigned>(Choice::Restore), itemtags::EffectTags(magicka, 1)), "new Restore choice index");
	std::printf("%d gear piece checks passed\n", checks);
}
