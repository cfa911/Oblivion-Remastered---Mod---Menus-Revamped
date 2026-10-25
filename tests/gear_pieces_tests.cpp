#include "../sortingoverhaul/GearPieces.h"
#include "../sortingoverhaul/ItemTags.h"

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
	check(itemtags::Info(itemtags::Tab::Weapons).count == 5 && itemtags::Info(itemtags::Tab::Gear).count == 9 &&
		itemtags::Info(itemtags::Tab::Potions).count == 6 && itemtags::Info(itemtags::Tab::Misc).count == 6, "choice counts");
	// Gear tags still agree with the captured masks: Ring is choice 1, Shield choice 8.
	check(itemtags::Includes(1, itemtags::GearTags(0xC0)) && itemtags::Includes(7, itemtags::GearTags(12)) && !itemtags::Includes(6, itemtags::GearTags(8)), "gear tags");
	check(itemtags::Includes(0, 0), "All keeps untagged items");
	check(!itemtags::Includes(1, 0), "untagged item is not choice 1");
	// Weapons: blade 0/1, blunt 2/3, staff 4, bow 5.
	check(itemtags::Includes(1, itemtags::WeaponTags(0)) && itemtags::Includes(1, itemtags::WeaponTags(1)), "blades");
	check(itemtags::Includes(2, itemtags::WeaponTags(2)) && itemtags::Includes(2, itemtags::WeaponTags(3)), "blunts");
	check(itemtags::Includes(3, itemtags::WeaponTags(5)) && itemtags::Includes(4, itemtags::WeaponTags(4)), "bow and staff");
	check(!itemtags::Includes(2, itemtags::WeaponTags(0)) && itemtags::WeaponTags(-1) == 0 && itemtags::WeaponTags(9) == 0, "weapon type isolation");
	// Potions: codes seen in the capture (REHE restore health, FOHE fortify health, REFA, CUPO, DGHE damage health).
	using itemtags::Code;
	const std::uint32_t rest[] = { Code('R', 'E', 'H', 'E') };
	const std::uint32_t mixed[] = { Code('R', 'E', 'H', 'E'), Code('D', 'G', 'F', 'A') };
	const std::uint32_t cure[] = { Code('C', 'U', 'P', 'O') };
	const std::uint32_t fatigue[] = { Code('R', 'E', 'F', 'A') };
	const std::uint32_t damage[] = { Code('D', 'G', 'H', 'E') };
	check(Code('R', 'S', 'M', 'A') == 0x414D5352u, "effect code byte order matches the dump");
	check(itemtags::Includes(1, itemtags::PotionTags(rest, 1, 0)) && !itemtags::Includes(5, itemtags::PotionTags(rest, 1, 0)), "restore health");
	check(itemtags::Includes(1, itemtags::PotionTags(mixed, 2, 1)) && itemtags::Includes(5, itemtags::PotionTags(mixed, 2, 1)), "mixed potion is health and poison");
	check(itemtags::Includes(4, itemtags::PotionTags(cure, 1, 0)) && itemtags::Includes(3, itemtags::PotionTags(fatigue, 1, 0)), "cure and fatigue");
	check(itemtags::Includes(5, itemtags::PotionTags(damage, 1, 2)) && !itemtags::Includes(1, itemtags::PotionTags(damage, 1, 2)), "poison is not health");
	check(itemtags::PotionTags(nullptr, 0, 0) == 0, "ingredient has no potion tag");
	// Misc: key 39, soul gem 38, book 21, misc 27 (lockpick 0xA), light 26, sigil stone 42, apparatus 19 (potions page).
	check(itemtags::Includes(1, itemtags::MiscTags(39, 0x123, 0)), "key");
	check(itemtags::Includes(2, itemtags::MiscTags(38, 0x2CF9F, 0)), "soul gem");
	check(itemtags::Includes(3, itemtags::MiscTags(21, 0x5, 1)), "book or scroll");
	check(itemtags::Includes(4, itemtags::MiscTags(27, 0x0A, 0)) && itemtags::Includes(4, itemtags::MiscTags(27, 0xFF00000A, 0)), "lockpick with any load-order byte");
	check(itemtags::Includes(5, itemtags::MiscTags(27, 0x191, 0)) && !itemtags::Includes(4, itemtags::MiscTags(27, 0x191, 0)), "other misc");
	check(itemtags::Includes(5, itemtags::MiscTags(26, 0x2CF9F, 0)) && itemtags::Includes(5, itemtags::MiscTags(42, 0x41FD3, 0)), "light and sigil stone are other");
	check(itemtags::MiscTags(19, 1, 0) == 0 && itemtags::MiscTags(25, 1, 0) == 0, "apparatus and ingredients stay untagged");
	// All Items: category from form type, then the sub-choice inside it.
	check(itemtags::Info(itemtags::Tab::AllItems).count == 5, "category count");
	check(itemtags::Category(33) == 1 && itemtags::Category(20) == 2 && itemtags::Category(22) == 2 && itemtags::Category(40) == 3 &&
		itemtags::Category(25) == 3 && itemtags::Category(19) == 3 && itemtags::Category(39) == 4 && itemtags::Category(21) == 4 && itemtags::Category(34) == 0, "categories");
	const std::uint32_t ring = itemtags::GearTags(0x40) | (2u << 28), bow = itemtags::WeaponTags(5) | (1u << 28), key = itemtags::MiscTags(39, 1, 0) | (4u << 28), ammo = 0;
	check(itemtags::IncludesAll(0, 0, ammo) && itemtags::IncludesAll(0, 3, bow), "no category keeps everything");
	check(itemtags::IncludesAll(2, 0, ring) && !itemtags::IncludesAll(2, 0, bow), "whole gear category");
	check(itemtags::IncludesAll(2, 1, ring) && !itemtags::IncludesAll(2, 2, ring), "ring inside gear");
	check(itemtags::IncludesAll(1, 3, bow) && !itemtags::IncludesAll(1, 3, key), "bow only inside weapons");
	check(itemtags::IncludesAll(4, 1, key) && !itemtags::IncludesAll(3, 1, key), "key only in misc");
	check(!itemtags::IncludesAll(1, 0, ammo) && itemtags::Includes(1, ring), "category bits do not disturb choice bits");
	// Magic: school/power and effect tags.
	check(itemtags::Info(itemtags::Tab::MagicSchool).count == 8 && itemtags::Info(itemtags::Tab::MagicEffect).count == magiceffects::kChoiceCount, "magic choice counts");
	constexpr std::array<std::string_view, 8> schoolLabels{ "All", "Alte", "Conj", "Dest", "Illu", "Myst", "Rest", "Powers" };
	for (unsigned i = 0; i < schoolLabels.size(); ++i) check(itemtags::SchoolPopupLabel(i) == schoolLabels[i], "exact school display label");
	check(itemtags::SchoolPopupLabel(8).empty(), "out-of-range school label is empty");
	check(std::string_view(itemtags::Info(itemtags::Tab::MagicSchool).names[1]) == "Alteration", "full school tooltip remains available");
	check(itemtags::Includes(3, itemtags::SchoolTags(2, 0)) && !itemtags::Includes(1, itemtags::SchoolTags(2, 0)), "destruction school");
	check(itemtags::Includes(6, itemtags::SchoolTags(5, 0)) && !itemtags::Includes(7, itemtags::SchoolTags(5, 0)), "restoration is not a power");
	check(itemtags::Includes(7, itemtags::SchoolTags(5, 2)) && itemtags::Includes(6, itemtags::SchoolTags(5, 2)), "a power keeps its school");
	check(itemtags::SchoolTags(9, 0) == 0, "unknown school is untagged");
	using magiceffects::Choice;
	const std::uint32_t fire[] = { Code('F', 'I', 'D', 'G') }, magicka[] = { Code('R', 'E', 'S', 'P') };
	check(itemtags::EffectTags(fire, 1) == magiceffects::Bit(Choice::Fire), "fire damage only");
	check(itemtags::EffectTags(magicka, 1) == magiceffects::Bit(Choice::Restore), "verified Restore Magicka code");
	check(itemtags::Includes(static_cast<unsigned>(Choice::Restore), itemtags::EffectTags(magicka, 1)), "new Restore choice index");
	std::printf("%d gear piece checks passed\n", checks);
}
