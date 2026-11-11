#pragma once
// The Custom pack's build manifest check (tests/custom_pack_manifest_tests.cpp). tools/make_dark_pack.py writes
// MRCustom_P.json next to the pack: "game_build" and "style" (the baked colour values by element name, the tab greys by their
// [Tab] name). At load the plugin compares them with the running game and CustomColours.ini (owner decision 6, log only):
// another game build = do not install (a cooked asset from another build can crash); changed colours = install the last pack,
// one warning; a missing or broken manifest = do not install (its build cannot be checked).
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "core/rules/CustomColours.h"

namespace custompack
{
	struct Manifest
	{
		bool                                                       ok = false;
		std::string                                                gameBuild;
		std::vector<std::pair<std::string, std::vector<float>>> style;
	};

	inline constexpr std::string_view kLaurelTint = "Skin.pattern";

	inline bool HasBakedLaurelTint(const Manifest& m)
	{
		for (const auto& [key, values] : m.style)
			if (key == kLaurelTint && values.size() == 4)
				return true;
		return false;
	}

	enum class Verdict { Current, ColoursChanged, WrongBuild, Unreadable };

	struct Decision
	{
		Verdict                  verdict = Verdict::Unreadable;
		std::vector<std::string> changed;   // ColoursChanged: the element names that differ
		bool                     install = false;
	};

	namespace detail
	{
		struct Reader
		{
			std::string_view s;
			size_t           i = 0;
			void             Space() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i; }
			bool             Eat(char c) { Space(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
			bool             String(std::string& out)
			{
				if (!Eat('"')) return false;
				const auto end = s.find('"', i);   // our manifest has no escaped quotes
				if (end == std::string_view::npos) return false;
				out.assign(s.substr(i, end - i));
				i = end + 1;
				return true;
			}
			bool Number(float& out)
			{
				Space();
				const auto [end, error] = std::from_chars(s.data() + i, s.data() + s.size(), out);
				if (error != std::errc()) return false;
				i = static_cast<size_t>(end - s.data());
				return true;
			}
		};

		// the value of the first top-level-looking `"key":` that is followed by `open` ('"' or '{')
		inline size_t FindKey(std::string_view s, std::string_view key, char open)
		{
			const std::string quoted = "\"" + std::string(key) + "\"";
			for (size_t at = s.find(quoted); at != std::string_view::npos; at = s.find(quoted, at + 1)) {
				Reader r{ s, at + quoted.size() };
				if (r.Eat(':')) {
					r.Space();
					if (r.i < s.size() && s[r.i] == open) return r.i;
				}
			}
			return std::string_view::npos;
		}
	}

	inline Manifest ParseManifest(std::string_view json)
	{
		Manifest m;
		const auto build = detail::FindKey(json, "game_build", '"');
		const auto style = detail::FindKey(json, "style", '{');   // the textures' "style" lists are followed by '['
		if (build == std::string_view::npos || style == std::string_view::npos) return m;
		detail::Reader r{ json, build };
		if (!r.String(m.gameBuild) || m.gameBuild.empty()) return m;
		r.i = style;
		if (!r.Eat('{')) return m;
		if (!r.Eat('}')) {
			do {
				std::string key;
				if (!r.String(key) || !r.Eat(':')) return m;
				std::vector<float> values;
				if (r.Eat('[')) {
					do {
						float v = 0;
						if (!r.Number(v)) return m;
						values.push_back(v);
					} while (r.Eat(','));
					if (!r.Eat(']')) return m;
				} else {
					float v = 0;
					if (!r.Number(v)) return m;
					values.push_back(v);
				}
				m.style.emplace_back(std::move(key), std::move(values));
			} while (r.Eat(','));
			if (!r.Eat('}')) return m;
		}
		m.ok = true;
		return m;
	}

	// a manifest style key: a [Colours] element or a [Tab] grey (the baked values)
	inline const customcolours::Entry* Baked(std::string_view key)
	{
		if (key == kLaurelTint) return customcolours::Find("Skin", "pattern");
		if (const auto* e = customcolours::Find("Colours", key)) return e;
		return customcolours::Find("Tab", key);
	}
	inline bool Known(std::string_view key) { return Baked(key) != nullptr; }

	inline Decision Decide(const Manifest& m, std::string_view runningBuild, darkstyle::Style colours)
	{
		Decision d;
		if (!m.ok) return d;   // Unreadable, not installed
		if (m.gameBuild != runningBuild) {
			d.verdict = Verdict::WrongBuild;
			return d;
		}
		d.install = true;
		for (const auto& [key, values] : m.style) {
			const auto* entry = Baked(key);
			if (!entry) continue;   // a value this plugin does not know (a newer builder): not ours to judge
			const float* now = entry->at(colours);
			bool         same = static_cast<int>(values.size()) == customcolours::Count(entry->kind);
			for (size_t i = 0; same && i < values.size(); ++i) same = std::fabs(values[i] - now[i]) <= 1e-4f;
			if (!same) d.changed.push_back(key);
		}
		d.verdict = d.changed.empty() ? Verdict::Current : Verdict::ColoursChanged;
		return d;
	}
}
