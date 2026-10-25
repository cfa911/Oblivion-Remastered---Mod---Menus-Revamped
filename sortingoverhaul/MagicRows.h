#pragma once
#include "ItemTags.h"
#include <cstddef>
#include <cstdint>

// Pure row-to-snapshot policy shared by production and host tests.
// UI Category is deliberately not an input: it is not a native spell type.
namespace magicrows
{
	constexpr std::uint64_t kPassAll = 1ull << 63;
	struct Spell
	{
		std::uint32_t codes[8]{};
		unsigned count = 0, type = ~0u;
		bool valid = false;
	};
	constexpr bool CastableRange(unsigned range) { return range == 1 || range == 2 || range == 4; }
	constexpr std::uint64_t Mask(unsigned range, unsigned school, int index, const Spell* spells, std::size_t size)
	{
		// Range8 is an Active effect. Unknown/unresolved rows retain the existing safe fallback.
		if (!CastableRange(range) || !spells || index < 1 || static_cast<std::size_t>(index) > size) return kPassAll;
		const auto& spell = spells[static_cast<std::size_t>(index) - 1];
		if (!spell.valid || spell.count > 8 || !itemtags::IsCastableSpell(spell.type)) return kPassAll;
		return static_cast<std::uint64_t>(itemtags::SchoolTags(school, spell.type)) |
			(static_cast<std::uint64_t>(itemtags::EffectTags(spell.codes, spell.count)) << 32);
	}
	constexpr bool Passes(std::uint64_t mask, unsigned schoolChoice, unsigned effectChoice)
	{
		return (mask & kPassAll) || (itemtags::Includes(schoolChoice, static_cast<std::uint32_t>(mask)) &&
			itemtags::Includes(effectChoice, static_cast<std::uint32_t>(mask >> 32)));
	}
}
