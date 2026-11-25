#pragma once
// In-game Pack Builder (Phase 3a): one asset baked from its stock files (pipeline.bake without the file system) and the
// manifest (pipeline.write_manifest: the same keys and order as the Python builder's MRCustom_P.json). The file work, retoc
// and the thread are in PackJob.cpp. Pure: tests/ingame_pack_core_tests.cpp, tests/ingame_pack_parity_tests.py.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <string>
#include <string_view>
#include <vector>

#include "builder/PackRecipes.h"
#include "builder/PackSha256.h"

namespace builder::pack
{
	inline constexpr const char* kGameBuild = "1.512.105.0";   // pipeline.GAME_BUILD
	inline constexpr const char* kBuilderVersion = "0.5.0";    // pack_builder.VERSION
	inline constexpr const char* kLaurelStockSha256 = "62be812455818cd56e241058bd2093659a3e5d94e3eab616f35998f485cb89ef";

	struct PackageFiles
	{
		Bytes uasset, uexp, ubulk;
		bool  hasUbulk = false;
	};

	enum class BakeResult { Baked, Skipped, Later };

	struct BakedAsset
	{
		PackageFiles             files;   // what goes into the stage folder
		std::vector<std::string> keys;    // the values it read (recipe order)
		json::Value              entry;   // the manifest's textures entry
	};

	inline json::Value ValueJson(const model::Value& a_v)   // a colour-file value as the manifest writes it (float or list)
	{
		if (a_v.n == 1) return json::MakeFloat(a_v.v[0]);
		auto list = json::MakeArray();
		for (int i = 0; i < a_v.n; ++i) list.items.push_back(json::MakeFloat(a_v.v[static_cast<size_t>(i)]));
		return list;
	}

	inline BakeResult BakeAsset(const std::string& a_asset, const std::string& a_recipe, const PackageFiles& a_stock, const Values& a_values,
		const PackData& a_data, BakedAsset& a_out, std::string& a_reason)
	{
		a_out = {};
		const auto name = AssetName(a_asset);
		const auto kind = std::string(Partition(a_recipe).first);
		const auto info = Recipe(a_recipe, a_data);
		if (info.kind == RecipeKind::Unknown) {
			a_reason = info.error;
			return BakeResult::Skipped;
		}
		if (info.kind == RecipeKind::Later) {
			a_reason = "recipe " + a_recipe + " is ported in a later phase";
			return BakeResult::Later;
		}
		for (const auto& k : info.keys)
			if (!a_values.count(k)) {
				a_reason = "KeyError: '" + k + "'";
				return BakeResult::Skipped;
			}
		if (kind == "laurels" && Sha256Hex(a_stock.uexp) != kLaurelStockSha256) {
			a_reason = "laurel stock asset differs from the verified game build";
			return BakeResult::Skipped;
		}
		PackageFiles files;
		if (info.kind == RecipeKind::Package) {
			if (a_data.importSwaps.count(name)) {   // the uasset import rewrite comes with the package rewrite (Phase 3d)
				a_reason = "import swaps are ported in a later phase";
				return BakeResult::Later;
			}
			if (!BakePackage(a_recipe, name, a_stock.uexp, a_values, a_data, files.uexp, a_reason)) return BakeResult::Skipped;
			files.uasset = a_stock.uasset;
		} else {
			TexturePackage tex;
			std::string    why;
			if (!ParseTexture(a_stock.uasset, a_stock.uexp, a_stock.hasUbulk ? &a_stock.ubulk : nullptr, tex, why)) {
				a_reason = why;
				return BakeResult::Skipped;
			}
			if (!FormatSupported(kind, tex.format)) {
				a_reason = "the stock texture is not BC3 (game update?)";
				return BakeResult::Skipped;
			}
			Dds stock{ tex.format, tex.width, tex.height, static_cast<int>(tex.mips.size()), GatherMips(tex, a_stock.uexp, a_stock.hasUbulk ? &a_stock.ubulk : nullptr) };
			Bytes baked;
			if (!BakeTexture(a_recipe, stock, a_values, baked, a_reason)) return BakeResult::Skipped;
			files = a_stock;
			if (!InjectMips(tex, baked, files.uasset, files.uexp, files.hasUbulk ? &files.ubulk : nullptr, a_reason)) return BakeResult::Skipped;
		}
		a_out.files = std::move(files);
		a_out.keys = info.keys;
		auto sorted = info.keys;
		std::sort(sorted.begin(), sorted.end());
		sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
		auto style = json::MakeArray();
		for (const auto& k : sorted) style.items.push_back(json::MakeString(k));
		a_out.entry = json::MakeObject();
		json::Set(a_out.entry, "recipe", json::MakeString(a_recipe));
		json::Set(a_out.entry, "style", style);
		json::Set(a_out.entry, "stock_uexp_sha256", json::MakeString(Sha256Hex(a_stock.uexp)));
		json::Set(a_out.entry, "baked_uexp_sha256", json::MakeString(Sha256Hex(a_out.files.uexp)));
		return BakeResult::Baked;
	}

	// the manifest body, filled while baking (pipeline._build: style in first-read order, textures in data order)
	struct BuildRecord
	{
		std::vector<std::string> styleOrder;
		Values                   style;
		json::Value              textures = json::MakeObject();
		json::Value              skipped = json::MakeArray();

		void Add(const std::string& a_asset, const BakedAsset& a_baked, const Values& a_values)
		{
			for (const auto& k : a_baked.keys) {
				if (!style.count(k)) styleOrder.push_back(k);
				style[k] = a_values.at(k);
			}
			json::Set(textures, a_asset, a_baked.entry);
		}
		void Skip(const std::string& a_asset, const std::string& a_recipe, const std::string& a_reason)
		{
			auto row = json::MakeObject();
			json::Set(row, "asset", json::MakeString(a_asset));
			json::Set(row, "recipe", json::MakeString(a_recipe));
			json::Set(row, "reason", json::MakeString(a_reason));
			skipped.items.push_back(std::move(row));
		}
	};

	struct PackFileHash
	{
		std::string name, sha256;
	};

	inline json::Value Manifest(const std::string& a_pack, const std::string& a_builtBy, const BuildRecord& a_record, const std::vector<PackFileHash>& a_files)
	{
		auto m = json::MakeObject();
		json::Set(m, "pack", json::MakeString(a_pack));
		json::Set(m, "game_build", json::MakeString(kGameBuild));
		json::Set(m, "built_by", json::MakeString(a_builtBy));
		json::Set(m, "builder_version", json::MakeString(kBuilderVersion));
		auto style = json::MakeObject();
		for (const auto& k : a_record.styleOrder) json::Set(style, k, ValueJson(a_record.style.at(k)));
		json::Set(m, "style", style);
		json::Set(m, "textures", a_record.textures);
		json::Set(m, "skipped", a_record.skipped);
		auto files = json::MakeObject();
		for (const auto& f : a_files) json::Set(files, f.name, json::MakeString(f.sha256));
		json::Set(m, "files", files);
		return m;
	}
}
