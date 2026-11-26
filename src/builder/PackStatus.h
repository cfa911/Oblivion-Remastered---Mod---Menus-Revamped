#pragma once
// In-game Pack Builder (Phase 3e): the "Build pack to apply" status of the builder window (tools/pack_builder/model.py
// needs_build and sizes_changed, window.py refresh_status): which baked values differ from the installed packs' manifests.
// Pure: tests/ingame_pack_status_tests.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <string>
#include <vector>

#include "builder/ColourModel.h"
#include "builder/PackJson.h"

namespace builder::pack
{
	namespace status
	{
		inline bool BakedSkin(const std::string& a_name)   // custom_colours.BAKED_SKIN
		{
			return a_name == "pattern" || a_name == "scrollTrack" || a_name == "scrollBorder" || a_name == "scrollInside" || a_name == "ornament" ||
			       a_name == "emphasisInk";
		}

		inline bool Same(const model::Value& a_v, const json::Value& a_j)   // model.same: lists (or one number), tolerance 1e-4
		{
			std::vector<double> b;
			if (a_j.type == json::Type::Array)
				for (const auto& x : a_j.items) b.push_back(x.Number());
			else b.push_back(a_j.Number());
			if (static_cast<int>(b.size()) != a_v.n) return false;
			for (int i = 0; i < a_v.n; ++i)
				if (!(std::fabs(a_v.v[static_cast<size_t>(i)] - b[static_cast<size_t>(i)]) <= model::kTolerance)) return false;
			return true;
		}
	}

	// model.needs_build: the baked keys ([Colours]/[Tab] names, "Skin.<name>" for the baked tints) whose value differs from the
	// colour pack manifest's "style" (a_style: its "style" object, nullptr when there is no pack: every baked tint then counts)
	inline std::vector<std::string> NeedsBuild(const std::vector<model::Entry>& a_entries, const json::Value* a_style)
	{
		std::vector<std::string> changed;
		for (const auto& e : a_entries) {
			std::string key;
			if (e.section == "Skin") {
				if (!status::BakedSkin(e.name)) continue;
				key = "Skin." + e.name;
			} else key = e.name;
			const json::Value* found = a_style ? a_style->Find(key) : nullptr;
			if (!found) {
				if (e.section == "Skin") changed.push_back(key);
				continue;
			}
			if (!status::Same(e.value, *found)) changed.push_back(key);
		}
		return changed;
	}

	// model.sizes_changed: the [Sizes] keys (text role scales excepted) that differ from the layout pack manifest's "sizes"
	inline std::vector<std::string> SizesChanged(const model::ColourModel& a_model, const json::Value* a_baked)
	{
		std::vector<std::string> changed;
		for (const auto& s : model::detail::SizeDefaults()) {
			if (model::detail::IsTextRole(s.key)) continue;
			const json::Value* baked = a_baked ? a_baked->Find(s.key) : nullptr;
			if (!baked || std::fabs(baked->Number() - a_model.Size(s.key)) > model::kTolerance) changed.push_back(s.key);
		}
		return changed;
	}

	// window.refresh_status: "Build pack to apply: <labels>[, sizes]" or "The packs match the colours and sizes."
	inline std::string BuildStatus(const std::vector<model::Entry>& a_entries, const std::vector<std::string>& a_stale, bool a_sizes)
	{
		std::vector<std::string> parts;
		for (const auto& key : a_stale)
			for (const auto& e : a_entries)
				if ((e.section == "Skin" ? "Skin." + e.name : e.name) == key) {
					parts.push_back(e.help);
					break;
				}
		if (a_sizes) parts.push_back("sizes");
		if (parts.empty()) return "The packs match the colours and sizes.";
		std::string out = "Build pack to apply: ";
		for (size_t i = 0; i < parts.size(); ++i) out += (i ? ", " : "") + parts[i];
		return out;
	}
}
