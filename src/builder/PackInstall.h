#pragma once
// In-game Pack Builder (Phase 3e): putting a built pack in place (tools/pack_builder/install.py: the four files together, the
// previous set kept as .bak, a failure half-way puts the previous set back, so CustomPack never holds a half-written pack),
// the [Layout] bLayoutPack switch (app.layout_enabled) and the build summary (app.summary). The file calls come in through
// FileOps, so the rules are tested without a disk: tests/ingame_pack_install_tests.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "builder/PackData.h"
#include "builder/PackJson.h"

namespace builder::pack
{
	struct FileOps
	{
		std::function<bool(const std::filesystem::path&)>                               exists;
		std::function<bool(const std::filesystem::path&, const std::filesystem::path&)> replace;   // move, replacing the target
		std::function<void(const std::filesystem::path&)>                               remove;    // delete if present
	};

	// install.install: moves <pack>.pak/.utoc/.ucas/.json from a_built into a_target; false + a_error (the installed set unchanged)
	inline bool InstallPack(const std::filesystem::path& a_built, const std::filesystem::path& a_target, const std::string& a_pack, const FileOps& a_ops,
		std::string& a_error)
	{
		static constexpr const char* exts[] = { "pak", "utoc", "ucas", "json" };
		std::vector<std::string> names, missing;
		for (const char* ext : exts) names.push_back(a_pack + "." + ext);
		for (const auto& n : names)
			if (!a_ops.exists(a_built / n)) missing.push_back(n);
		if (!missing.empty()) {
			std::string list;
			for (size_t i = 0; i < missing.size(); ++i) list += (i ? ", " : "") + missing[i];
			a_error = "the new pack is incomplete (missing " + list + "): the installed pack was not changed";
			return false;
		}
		for (const auto& n : names) a_ops.remove(a_target / (n + ".bak"));   // the backup of the build before last goes first
		std::vector<std::string> backed, placed;
		bool                     ok = true;
		for (const auto& n : names) {
			if (!a_ops.exists(a_target / n)) continue;
			if (!a_ops.replace(a_target / n, a_target / (n + ".bak"))) {
				ok = false;
				break;
			}
			backed.push_back(n);
		}
		for (size_t i = 0; ok && i < names.size(); ++i) {
			if (!a_ops.replace(a_built / names[i], a_target / names[i])) {
				ok = false;
				break;
			}
			placed.push_back(names[i]);
		}
		if (ok) return true;
		for (const auto& n : placed) a_ops.remove(a_target / n);
		for (const auto& n : backed) a_ops.replace(a_target / (n + ".bak"), a_target / n);
		a_error = "could not replace the pack in " + a_target.string() + "; the previous pack was put back. Close the game and build again.";
		return false;
	}

	// app.layout_enabled: [Layout] bLayoutPack of MenusRevamped.ini (configparser: section names case-sensitive, keys not; inline
	// ';'/'#' comments after whitespace; a missing file, section or key = 1)
	inline bool LayoutEnabled(std::string_view a_ini)
	{
		if (a_ini.starts_with("\xEF\xBB\xBF")) a_ini.remove_prefix(3);
		std::string section, value = "1";
		while (!a_ini.empty()) {
			const auto stop = a_ini.find('\n');
			auto       line = model::Strip(a_ini.substr(0, stop));
			a_ini = stop == std::string_view::npos ? std::string_view{} : a_ini.substr(stop + 1);
			if (line.empty() || line.front() == ';' || line.front() == '#') continue;
			if (line.front() == '[' && line.find(']') != std::string_view::npos) {
				section = std::string(line.substr(1, line.find(']') - 1));
				continue;
			}
			const auto eq = line.find_first_of("=:");
			if (section != "Layout" || eq == std::string_view::npos) continue;
			std::string key(model::Strip(line.substr(0, eq)));
			std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (key != "blayoutpack") continue;
			auto v = line.substr(eq + 1);
			for (size_t i = 1; i < v.size(); ++i)
				if ((v[i] == ';' || v[i] == '#') && (v[i - 1] == ' ' || v[i - 1] == '\t')) {
					v = v.substr(0, i);
					break;
				}
			value = std::string(model::Strip(v));
		}
		return value != "0";
	}

	// app.summary: what the build log and the result say (a_layout: the MRLayout_P manifest, nullptr when not built)
	inline std::string BuildSummary(const json::Value& a_colour, const json::Value* a_layout, const std::string& a_target)
	{
		auto count = [](const json::Value& m, const char* k) {
			const auto* v = m.Find(k);
			return v ? v->items.size() : size_t{ 0 };
		};
		auto str = [](const json::Value& m, const char* k) {
			const auto* v = m.Find(k);
			return v ? v->s : std::string();
		};
		std::vector<const json::Value*> skipped;
		if (const auto* s = a_colour.Find("skipped"))
			for (const auto& row : s->items) skipped.push_back(&row);
		std::string out = str(a_colour, "pack") + " built: " + std::to_string(count(a_colour, "textures")) + " asset(s) baked, " +
		                  std::to_string(count(a_colour, "skipped")) + " skipped.";
		if (a_layout) {
			out += "\n" + str(*a_layout, "pack") + " built: " + std::to_string(count(*a_layout, "textures")) + " package(s) with the sizes, " +
			       std::to_string(count(*a_layout, "skipped")) + " skipped.";
			if (const auto* s = a_layout->Find("skipped"))
				for (const auto& row : s->items) skipped.push_back(&row);
		}
		out += "\nInstalled in " + a_target +
		       ". Start the game: the plugin installs this colour pack for the Custom preset (uPreset=2), the shipped Dark pack for Dark "
		       "(uPreset=1) and the sizes pack for Classic (uPreset=0).";
		if (!skipped.empty()) {
			out += "\nSkipped (the game's stock look shows for these):";
			for (const auto* row : skipped) out += "\n  " + AssetName(str(*row, "asset")) + ": " + str(*row, "reason");
		}
		return out;
	}
}
