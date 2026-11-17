#pragma once
// The preset packs (tests/preset_packs_tests.cpp; owner 2026-11-17, decision 48 extension). uPreset 0 Classic, 1 Dark,
// 2 Custom. Two folders, two names: the zip ships Plugins/MenusRevamped/DarkPack/MRDark_P.* (the default colours); the Pack
// Builder writes Plugins/MenusRevamped/CustomPack/MRCustom_P.* (and MRLayout_P.*), never shipped, so a player build never
// overwrites a Vortex-managed file. At plugin load, before the engine mounts ~mods: Classic removes both colour packs; Dark
// installs MRDark_P; Custom installs MRCustom_P when the player built a valid one, otherwise MRDark_P. Exactly one colour
// pack is in ~mods; the sizes pack MRLayout_P only when no colour pack is (a game package comes from one pak only).
#include <filesystem>
#include "core/rules/DarkPackSwitch.h"

namespace presetpacks
{
	inline constexpr const wchar_t* kDarkPack = L"MRDark_P";
	inline constexpr const wchar_t* kCustomPack = L"MRCustom_P";
	inline constexpr const wchar_t* kLayoutPack = L"MRLayout_P";

	enum class Colours { None, Dark, Custom };

	// preset: 0 Classic, 1 Dark, 2 Custom. customAllowed: the player's pack is present and its manifest allows it;
	// darkAllowed: the same for the shipped Dark pack.
	inline constexpr Colours Choose(int preset, bool customAllowed, bool darkAllowed)
	{
		if (preset == 2 && customAllowed) return Colours::Custom;
		if (preset == 1 || preset == 2) return darkAllowed ? Colours::Dark : Colours::None;
		return Colours::None;
	}

	// all three container files are in the folder
	inline bool Present(const std::filesystem::path& source, const wchar_t* name)
	{
		std::error_code error;
		for (const auto* extension : darkpackswitch::kExtensions)
			if (!std::filesystem::is_regular_file(source / (std::wstring(name) + extension), error)) return false;
		return true;
	}

	struct Outcomes
	{
		darkpackswitch::Outcome dark;
		darkpackswitch::Outcome custom;
		darkpackswitch::Outcome layout;
	};

	// The pack not chosen goes first, so the two colour packs are never in ~mods together; the sizes pack last.
	inline Outcomes Switch(const std::filesystem::path& darkSource, const std::filesystem::path& customSource,
		const std::filesystem::path& mods, Colours colours, bool layout, bool layoutAllowed)
	{
		Outcomes out{};
		if (colours == Colours::Custom) {
			out.dark = darkpackswitch::Switch(darkSource, mods, false, kDarkPack);
			out.custom = darkpackswitch::Switch(customSource, mods, true, kCustomPack);
		} else {
			out.custom = darkpackswitch::Switch(customSource, mods, false, kCustomPack);
			out.dark = darkpackswitch::Switch(darkSource, mods, colours == Colours::Dark, kDarkPack);
		}
		const bool coloursIn = darkpackswitch::Active(out.dark) || darkpackswitch::Active(out.custom);
		out.layout = darkpackswitch::Switch(customSource, mods, !coloursIn && layout && layoutAllowed, kLayoutPack);
		return out;
	}
}
