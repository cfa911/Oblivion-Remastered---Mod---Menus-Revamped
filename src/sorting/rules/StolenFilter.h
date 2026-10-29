#pragma once
#include <cstdint>

// Stolen-only toggle: an icon beside the category picker in the Inventory and on the player's container/barter side.
namespace stolenfilter
{
	// OriginalInventoryMenuItemProperties.StatusFlags bit 0 = stolen (0x2 = enchanted). Proven on the Inventory and the
	// fence sell list, game 1.512.105.0, 2026-10-29 (KB docs/inventory/internals.md "StatusFlags values").
	constexpr std::uint32_t kStolenBit = 0x1;
	constexpr int kToggleCommand = -5;  // pickergrid uses -4 and -1..-3
	// Active background: owner's #DB675F at 50% opacity, sRGB converted to linear Slate colour. No outline.
	inline constexpr float kStolenFill[4] = { 0.708376f, 0.135633f, 0.114435f, 0.5f };
	constexpr bool Supports(int kind) { return kind == 0 || kind == 3; }  // picker kinds: Inventory, player side
	enum class Status : std::int8_t { Unknown = -1, Legal = 0, Stolen = 1 };
	constexpr Status FromFlags(std::uint32_t flags) { return (flags & kStolenBit) ? Status::Stolen : Status::Legal; }
	// A row whose status cannot be read stays visible, like an unresolved category.
	constexpr bool Keeps(bool on, Status status) { return !on || status != Status::Legal; }
}
