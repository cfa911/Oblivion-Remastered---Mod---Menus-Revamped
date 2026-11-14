#pragma once
// The sizes pack (tests/layout_pack_switch_tests.cpp). Custom pack builder Step B (owner 2026-11-12, decision 46): one Build
// writes MRCustom_P = colours + sizes and MRLayout_P = sizes only (Plugins/MenusRevamped/CustomPack). A game package can come
// from one pak only, so the plugin installs exactly one of them: MRCustom_P for uPreset=2 when it is present and allowed,
// otherwise MRLayout_P when [Layout] bLayoutPack=1 and its manifest (MRLayout_P.json: "game_build", "sizes") is for this game.
#include <filesystem>
#include <string>
#include <string_view>
#include "core/rules/DarkPackSwitch.h"

namespace layoutpack
{
	inline constexpr const wchar_t* kLayoutPack = L"MRLayout_P";

	struct Outcomes
	{
		darkpackswitch::Outcome custom;
		darkpackswitch::Outcome layout;
	};

	namespace detail
	{
		inline bool HasObject(std::string_view json, std::string_view key)   // "key": { ... at the top level of our manifests
		{
			const std::string quoted = "\"" + std::string(key) + "\"";
			for (auto at = json.find(quoted); at != std::string_view::npos; at = json.find(quoted, at + 1)) {
				auto i = at + quoted.size();
				while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) ++i;
				if (i < json.size() && json[i] == ':') {
					++i;
					while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) ++i;
					if (i < json.size() && json[i] == '{') return true;
				}
			}
			return false;
		}

		inline std::string GameBuild(std::string_view json)
		{
			const auto at = json.find("\"game_build\"");
			if (at == std::string_view::npos) return {};
			const auto open = json.find('"', json.find(':', at) + 1);
			const auto close = open == std::string_view::npos ? open : json.find('"', open + 1);
			return close == std::string_view::npos ? std::string{} : std::string(json.substr(open + 1, close - open - 1));
		}
	}

	// the colour pack carries the sizes (built with bLayoutPack=1 by a Step B builder)
	inline bool HasSizes(std::string_view manifest) { return detail::HasObject(manifest, "sizes"); }
	inline bool HasHeaderLayout(std::string_view manifest) { return detail::HasObject(manifest, "header_layout"); }
	inline bool HasHeaderOverflow(std::string_view manifest) { return detail::HasObject(manifest, "header_overflow"); }

	// MRLayout_P.json allows the pack: built for this game build, with its sizes
	inline bool LayoutManifestOk(std::string_view manifest, std::string_view runningBuild)
	{
		return !runningBuild.empty() && detail::GameBuild(manifest) == runningBuild && HasSizes(manifest);
	}

	// custom: uPreset=2; customAllowed: the colour pack's manifest check; layout: [Layout] bLayoutPack; layoutAllowed: the
	// layout manifest check. The colour pack goes first (it also removes the old MRDark_P); the layout pack only when the colour
	// pack is not in ~mods after that.
	inline Outcomes Switch(const std::filesystem::path& source, const std::filesystem::path& mods, bool custom, bool customAllowed,
		bool layout, bool layoutAllowed)
	{
		Outcomes out{};
		out.custom = darkpackswitch::SwitchCustom(source, mods, custom, customAllowed);
		const bool coloursIn = darkpackswitch::Active(out.custom);
		out.layout = darkpackswitch::Switch(source, mods, !coloursIn && layout && layoutAllowed, kLayoutPack);
		return out;
	}
}
