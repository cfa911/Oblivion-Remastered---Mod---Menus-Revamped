#pragma once
// In-game Pack Builder (Phase 3a, sizes Phase 3d): one asset baked from its stock files (pipeline.bake without the file
// system, with or without [Sizes]), the layout-only packages (pipeline.stage_layout, layout_entries) and the manifest
// (pipeline.write_manifest: the same keys and order as the Python builder's MRCustom_P.json and MRLayout_P.json). The file work, retoc
// and the thread are in PackJob.cpp. Pure: tests/ingame_pack_core_tests.cpp, tests/ingame_pack_parity_tests.py.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <string>
#include <string_view>
#include <vector>

#include <functional>

#include "builder/PackLayout.h"
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
		bool                     layoutSkipped = false;   // a bp_colour package whose sizes were refused (pipeline: "layout_skipped")
		std::string              layoutReason;
	};

	inline json::Value ValueJson(const model::Value& a_v)   // a colour-file value as the manifest writes it (float or list)
	{
		if (a_v.n == 1) return json::MakeFloat(a_v.v[0]);
		auto list = json::MakeArray();
		for (int i = 0; i < a_v.n; ++i) list.items.push_back(json::MakeFloat(a_v.v[static_cast<size_t>(i)]));
		return list;
	}

	// pipeline.bake: a_sizes = the [Sizes] values (nullptr: colours only); a colour-patched package in the layout section also
	// gets its sizes (a refusal keeps the colours: layoutSkipped), an import_swaps package its .uasset rewrite
	inline BakeResult BakeAsset(const std::string& a_asset, const std::string& a_recipe, const PackageFiles& a_stock, const Values& a_values,
		const PackData& a_data, BakedAsset& a_out, std::string& a_reason, const Sizes* a_sizes = nullptr)
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
		const bool   withLayout = a_sizes && a_data.layout.count(name);
		std::string  layoutError;
		if (info.kind == RecipeKind::Package) {
			if (!BakePackage(a_recipe, name, a_stock.uexp, a_values, a_data, files.uexp, a_reason)) return BakeResult::Skipped;
			files.uasset = a_stock.uasset;
			if (withLayout && !LayoutPackage(files.uasset, files.uexp, name, a_data, *a_sizes, layoutError) && layoutError.empty())
				layoutError = "layout refused";
			if (const auto swaps = a_data.swaps.find(name); swaps != a_data.swaps.end())   // RMI-096: the popup paper's plain texture
				if (!SwapImports(files.uasset, swaps->second, a_reason)) return BakeResult::Skipped;
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
		if (withLayout && kind == "bp_colour") {
			if (layoutError.empty()) json::Set(a_out.entry, "layout", json::MakeBool(true));
			else {
				json::Set(a_out.entry, "layout_skipped", json::MakeString(layoutError));
				a_out.layoutSkipped = true;
				a_out.layoutReason = layoutError;
			}
		}
		json::Set(a_out.entry, "stock_uexp_sha256", json::MakeString(Sha256Hex(a_stock.uexp)));
		json::Set(a_out.entry, "baked_uexp_sha256", json::MakeString(Sha256Hex(a_out.files.uexp)));
		return BakeResult::Baked;
	}

	// the manifest body, filled while baking (pipeline._build: style in first-read order, textures in data order; build_layout:
	// layoutPack, no style)
	struct BuildRecord
	{
		std::vector<std::string> styleOrder;
		Values                   style;
		json::Value              textures = json::MakeObject();
		json::Value              skipped = json::MakeArray();
		bool                     layoutPack = false;   // MRLayout_P (pipeline.build_layout): sizes, textures, skipped
		bool                     hasSizes = false;     // the body has "sizes" (built with [Sizes])
		json::Value              sizes;

		void SetSizes(const Sizes& a_sizes)
		{
			hasSizes = true;
			sizes = UsedSizes(a_sizes);
		}

		void Add(const std::string& a_asset, const BakedAsset& a_baked, const Values& a_values)
		{
			for (const auto& k : a_baked.keys) {
				if (!style.count(k)) styleOrder.push_back(k);
				style[k] = a_values.at(k);
			}
			json::Set(textures, a_asset, a_baked.entry);
			if (a_baked.layoutSkipped) Skip(a_asset, "layout", a_baked.layoutReason);
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

	namespace detail
	{
		inline bool Skipped(const json::Value& a_skipped, const std::string& a_asset)
		{
			for (const auto& row : a_skipped.items)
				if (const auto* a = row.Find("asset"); a && a->s == a_asset) return true;
			return false;
		}
		inline const std::string* LayoutAsset(const PackData* a_data, const char* a_package)
		{
			if (!a_data) return nullptr;
			const auto it = a_data->layout.find(a_package);
			return it == a_data->layout.end() ? nullptr : &it->second.asset;
		}
	}

	// pipeline.write_manifest; a_data (the layout section) decides header_layout and header_overflow when the body has sizes
	inline json::Value Manifest(const std::string& a_pack, const std::string& a_builtBy, const BuildRecord& a_record, const std::vector<PackFileHash>& a_files, const PackData* a_data = nullptr)
	{
		auto m = json::MakeObject();
		json::Set(m, "pack", json::MakeString(a_pack));
		json::Set(m, "game_build", json::MakeString(kGameBuild));
		json::Set(m, "built_by", json::MakeString(a_builtBy));
		json::Set(m, "builder_version", json::MakeString(kBuilderVersion));
		if (a_record.layoutPack) {
			if (a_record.hasSizes) json::Set(m, "sizes", a_record.sizes);
		} else {
			auto style = json::MakeObject();
			for (const auto& k : a_record.styleOrder) json::Set(style, k, ValueJson(a_record.style.at(k)));
			json::Set(m, "style", style);
		}
		json::Set(m, "textures", a_record.textures);
		json::Set(m, "skipped", a_record.skipped);
		if (!a_record.layoutPack && a_record.hasSizes) json::Set(m, "sizes", a_record.sizes);
		auto baked = [&](const std::string* a_asset) { return a_asset && a_record.textures.Find(*a_asset) && !detail::Skipped(a_record.skipped, *a_asset); };
		if (a_record.hasSizes && baked(detail::LayoutAsset(a_data, "WBP_ModernMenu_ContainerInventory_MainContent")) &&
			baked(detail::LayoutAsset(a_data, "WBP_ModernMenu_MagicMenu"))) {
			json::Set(m, "header_layout", json::MakeObject());
			if (baked(detail::LayoutAsset(a_data, "WBP_OriginalMenu_Inventory_FilterScrollbox"))) json::Set(m, "header_overflow", json::MakeObject());
		}
		auto files = json::MakeObject();
		for (const auto& f : a_files) json::Set(files, f.name, json::MakeString(f.sha256));
		json::Set(m, "files", files);
		return m;
	}

	struct StagedFiles
	{
		std::string  asset;
		PackageFiles files;
	};
	// reads a layout package's stock .uasset/.uexp (asset = path under Content); false: not extracted
	using ReadStock = std::function<bool(const std::string& a_asset, PackageFiles& a_out)>;

	// pipeline.stage_layout: one layout-only package (its manifest entry); false + the reason when a check fails
	inline bool StageLayout(const std::string& a_package, const PackData& a_data, const Sizes& a_sizes, const ReadStock& a_read, StagedFiles& a_out, json::Value& a_entry,
		std::string& a_reason)
	{
		const auto it = a_data.layout.find(a_package);
		if (it == a_data.layout.end()) {
			a_reason = "KeyError: '" + a_package + "'";
			return false;
		}
		PackageFiles stock;
		if (!a_read(it->second.asset, stock)) {
			a_reason = "not extracted from the game (game update or wrong folder?)";
			return false;
		}
		PackageFiles files{ stock.uasset, stock.uexp, {}, false };
		if (!LayoutPackage(files.uasset, files.uexp, a_package, a_data, a_sizes, a_reason)) return false;
		a_entry = json::MakeObject();
		json::Set(a_entry, "recipe", json::MakeString("layout"));
		json::Set(a_entry, "stock_uexp_sha256", json::MakeString(Sha256Hex(stock.uexp)));
		json::Set(a_entry, "baked_uexp_sha256", json::MakeString(Sha256Hex(files.uexp)));
		a_out = { it->second.asset, std::move(files) };
		return true;
	}

	// pipeline.layout_entries: every layout package that no colour recipe staged (by name, the record's textures so far) is
	// staged into a_staged with its entry, or skipped and listed. Call it after the colour assets (MRCustom_P with sizes) or on
	// an empty record with layoutPack set (MRLayout_P).
	inline void LayoutEntries(const PackData& a_data, const Sizes& a_sizes, const ReadStock& a_read, BuildRecord& a_record, std::vector<StagedFiles>& a_staged)
	{
		std::set<std::string> staged;
		for (const auto& asset : a_record.textures.keys) staged.insert(AssetName(asset));
		for (const auto& package : a_data.layoutPackages) {
			if (staged.count(package)) continue;
			StagedFiles out;
			json::Value entry;
			std::string reason;
			const auto  it = a_data.layout.find(package);
			const auto  asset = it == a_data.layout.end() ? package : it->second.asset;
			if (StageLayout(package, a_data, a_sizes, a_read, out, entry, reason)) {
				json::Set(a_record.textures, asset, entry);
				a_staged.push_back(std::move(out));
			} else
				a_record.Skip(asset, "layout", reason);
		}
	}
}
