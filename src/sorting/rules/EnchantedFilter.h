#pragma once
#include <cstdint>

// Native item StatusFlags, KB docs/inventory/internals.md (game 1.512.105.0).
namespace enchantedfilter
{
	constexpr std::uint32_t kEnchantedBit = 0x2;
	// sRGB #5F8FDB, converted to linear Slate colour; 50% opacity.
	inline constexpr float kEnchantedFill[4] = { 0.114435f, 0.274677f, 0.708376f, 0.5f };
	constexpr int kToggleCommand = -6;
	constexpr bool Supports(int kind) { return kind == 0 || kind == 3 || kind == 4; }
	enum class Status : std::int8_t { Unknown = -1, Plain = 0, Enchanted = 1 };
	constexpr Status FromFlags(std::uint32_t flags) { return (flags & kEnchantedBit) ? Status::Enchanted : Status::Plain; }
	constexpr bool Keeps(bool on, Status status) { return !on || status != Status::Plain; }
}
