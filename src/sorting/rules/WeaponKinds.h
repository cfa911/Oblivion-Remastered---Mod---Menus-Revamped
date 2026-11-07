#pragma once

#include <cstdint>
#include <string_view>

// Native type keeps the handedness/skill boundary. Subtype hints come only from
// model/icon filenames, never localized display text, speed/reach or guessed IDs.
namespace weaponkinds
{
	// Choice order = popup and category-sort order (owner 2026-11-07).
	enum class Kind : unsigned { All, Claymore, Longsword, Shortsword, Dagger, WarAxe, Battleaxe, Mace, Warhammer, Staff, Bow, Arrow, Other, Count, Ambiguous };
	constexpr unsigned kCount = static_cast<unsigned>(Kind::Count);
	constexpr std::uint32_t Bit(Kind kind)
	{
		const unsigned index = static_cast<unsigned>(kind);
		return index > 0 && index < kCount ? 1u << (index - 1) : 0u;
	}
	constexpr char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }
	constexpr std::string_view Filename(std::string_view path)
	{
		const auto slash = path.find_last_of("/\\");
		return slash == std::string_view::npos ? path : path.substr(slash + 1);
	}
	constexpr bool Contains(std::string_view text, std::string_view token)
	{
		if (text.size() < token.size()) return false;
		for (std::size_t i = 0; i <= text.size() - token.size(); ++i) {
			bool match = true;
			for (std::size_t j = 0; j < token.size(); ++j) if (Lower(text[i + j]) != token[j]) { match = false; break; }
			if (match) return true;
		}
		return false;
	}
	constexpr Kind Hint(std::string_view path)
	{
		const auto name = Filename(path);
		Kind found = Kind::Other;
		struct Token { std::string_view text; Kind kind; };
		constexpr Token tokens[]{ { "dagger", Kind::Dagger }, { "shortsword", Kind::Shortsword }, { "longsword", Kind::Longsword },
			{ "claymore", Kind::Claymore }, { "waraxe", Kind::WarAxe }, { "battleaxe", Kind::Battleaxe }, { "mace", Kind::Mace },
			{ "warhammer", Kind::Warhammer } };
		for (const auto& token : tokens) {
			if (!Contains(name, token.text)) continue;
			if (found != Kind::Other) return Kind::Ambiguous;
			found = token.kind;
		}
		return found;
	}
	constexpr bool Compatible(int type, Kind kind)
	{
		switch (kind) {
		case Kind::Dagger: case Kind::Shortsword: case Kind::Longsword: return type == 0;
		case Kind::Claymore: return type == 1;
		case Kind::WarAxe: case Kind::Mace: return type == 2;
		case Kind::Battleaxe: case Kind::Warhammer: return type == 3;
		default: return false;
		}
	}
	constexpr std::uint32_t Tags(int type, std::string_view model = {}, std::string_view icon = {})
	{
		if (type == 5) return Bit(Kind::Bow);
		if (type == 4) return Bit(Kind::Staff);
		const auto modelKind = Hint(model), iconKind = Hint(icon);
		if (modelKind == Kind::Ambiguous || iconKind == Kind::Ambiguous) return Bit(Kind::Other);
		// Disagreeing specific asset hints are ambiguous, including wrong-handed assets.
		if (modelKind != Kind::Other && iconKind != Kind::Other && modelKind != iconKind) return Bit(Kind::Other);
		const auto kind = modelKind != Kind::Other ? modelKind : iconKind;
		return Bit(Compatible(type, kind) ? kind : Kind::Other);
	}
}
