#pragma once

#include <cstdint>

namespace gearpieces
{
	// Choice order = popup and category-sort order: head to toe, then jewellery (owner 2026-11-07).
	enum class Piece : unsigned { All, Head, Torso, Arms, Legwear, Feet, Shield, Amulet, Ring };
	constexpr std::uint16_t Mask(Piece a_piece)
	{
		switch (a_piece) {
		case Piece::Ring: return 0x00C0;
		case Piece::Amulet: return 0x0100;
		case Piece::Head: return 0x0003;
		case Piece::Torso: return 0x0004;
		case Piece::Arms: return 0x0010;
		case Piece::Feet: return 0x0020;
		case Piece::Legwear: return 0x0008;
		case Piece::Shield: return 0x2000;
		default: return 0;
		}
	}
	constexpr bool Includes(Piece a_piece, std::uint16_t a_slots)
	{
		return a_piece == Piece::All || (Mask(a_piece) & a_slots) != 0;
	}
}
