#include "../src/sorting/rules/MagicRows.h"
#include <array>
#include <cstdio>
#include <cstdlib>

unsigned checks = 0;
void Check(bool ok, const char* label)
{
	++checks;
	if (!ok) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); }
}
int main()
{
	using magiceffects::Choice;
	using magiceffects::Code;
	using magiceffects::Bit;
	// First six native spell snapshots from magic-spells-20261024.txt.
	// School below is a synthetic row input; no claim that it was captured with this native list.
	std::array<magicrows::Spell, 6> spells{{
		{ { Code("FOAT"), Code("FOAT"), Code("FOAT"), Code("FOAT"), Code("FOAT"), Code("FOAT"), Code("FOAT"), Code("FOAT") }, 8, 2, true },
		{ { Code("FOSP"), Code("RSMA") }, 2, 2, true },
		{ { Code("INVI"), Code("FOSK"), Code("FOSK"), Code("FOSK") }, 4, 2, true },
		{ { Code("OPEN") }, 1, 2, true },
		{ { Code("FOSK") }, 1, 2, true },
		{ { Code("CALM") }, 1, 3, true },
	}};
	const std::uint32_t expected[] = {
		Bit(Choice::Fortify), Bit(Choice::Fortify) | Bit(Choice::Resistance),
		Bit(Choice::Stealth) | Bit(Choice::Fortify), Bit(Choice::Utility),
		Bit(Choice::Fortify), Bit(Choice::Control)
	};
	for (unsigned at = 0; at < spells.size(); ++at)
		for (unsigned range : { 1u, 2u, 4u })
			for (unsigned school = 0; school < 6; ++school) {
				const auto mask = magicrows::Mask(range, school, static_cast<int>(at + 1), spells.data(), spells.size());
				Check(!(mask & magicrows::kPassAll), "resolved powers must not bypass filters");
				Check(static_cast<std::uint32_t>(mask >> 32) == expected[at], "captured power effects classified");
				Check(static_cast<std::uint32_t>(mask) == ((1u << school) | (1u << 6)), "power keeps only its row school and Powers");
				Check(!magicrows::Passes(mask, 0, static_cast<unsigned>(Choice::Fire)), "none of six captured powers is Fire");
				for (unsigned schoolChoice = 0; schoolChoice < 8; ++schoolChoice)
					for (unsigned effectChoice = 0; effectChoice < magiceffects::kChoiceCount; ++effectChoice) {
						const bool wantedSchool = schoolChoice == 0 || schoolChoice == school + 1 || schoolChoice == 7;
						const bool wantedEffect = effectChoice == 0 || (expected[at] & (1u << (effectChoice - 1))) != 0;
						Check(magicrows::Passes(mask, schoolChoice, effectChoice) == (wantedSchool && wantedEffect), "combined School AND Effect policy");
					}
			}
	const auto greater = magicrows::Mask(4, 5, 2, spells.data(), spells.size());
	Check(magicrows::Passes(greater, 7, static_cast<unsigned>(Choice::Fortify)), "Gates of Aetherius under Powers+Fortify");
	Check(magicrows::Passes(greater, 6, static_cast<unsigned>(Choice::Resistance)), "Gates of Aetherius under its row school+Resistance");
	Check(!magicrows::Passes(greater, 1, 0), "greater power excluded from unrelated school");
	const auto lesser = magicrows::Mask(4, 3, 6, spells.data(), spells.size());
	Check(magicrows::Passes(lesser, 7, static_cast<unsigned>(Choice::Control)), "lesser power included under Powers+Control");
	magicrows::Spell normal{ { Code("FIDG") }, 1, 0, true };
	const auto fire = magicrows::Mask(1, 2, 1, &normal, 1);
	Check(magicrows::Passes(fire, 3, static_cast<unsigned>(Choice::Fire)), "normal spell still filters by school/effect");
	Check(!magicrows::Passes(fire, 7, 0), "ordinary spell is not a power");
	for (unsigned kind : { 1u, 4u, 5u, 6u, 7u, 8u, 9u, ~0u }) {
		auto invalid = normal; invalid.type = kind;
		Check(magicrows::Mask(4, 2, 1, &invalid, 1) == magicrows::kPassAll, "unsupported native type remains safe fallback");
	}
	for (unsigned range : { 0u, 3u, 5u, 8u, 255u })
		Check(magicrows::Mask(range, 2, 1, &normal, 1) == magicrows::kPassAll, "Active effects/unknown range not misclassified");
	for (int index : { -100, -1, 0, 2, 600 })
		Check(magicrows::Mask(4, 2, index, &normal, 1) == magicrows::kPassAll, "invalid index does not read snapshot");
	Check(magicrows::Mask(4, 2, 1, nullptr, 1) == magicrows::kPassAll, "null snapshot");
	Check(magicrows::Mask(4, 2, 1, &normal, 0) == magicrows::kPassAll, "empty snapshot");
	auto unread = normal; unread.valid = false;
	Check(magicrows::Mask(4, 2, 1, &unread, 1) == magicrows::kPassAll, "failed native read");
	unread = normal; unread.count = 9;
	Check(magicrows::Mask(4, 2, 1, &unread, 1) == magicrows::kPassAll, "effect array count bound");
	magicrows::Spell unknown{ { Code("CLRV") }, 1, 3, true };
	const auto other = magicrows::Mask(4, 3, 1, &unknown, 1);
	Check(other != magicrows::kPassAll && magicrows::Passes(other, 7, static_cast<unsigned>(Choice::Other)), "unknown mod effect is Other, not every family");
	Check(!magicrows::Passes(other, 0, static_cast<unsigned>(Choice::Fire)), "unknown effect does not bypass Fire");
	Check(itemtags::SchoolTags(5, 3) == ((1u << 5) | (1u << 6)), "lesser-power School tag");
	Check(itemtags::SchoolTags(9, 2) == (1u << 6), "unknown school power still belongs to Powers only");
	Check(itemtags::SchoolTags(5, 4) == 0, "native ability is not castable spell");
	std::printf("%u magic row checks passed\n", checks);
}
