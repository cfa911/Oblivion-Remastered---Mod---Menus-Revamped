#pragma once
// In-game Pack Builder Phase 4a: the pure part of the live art preview (src/builder/LivePreview.cpp does the threads, the
// game and the GPU). A colour change in the window becomes: the changed colour-file keys (inside the live roles) -> the data
// rows whose texture recipe reads one of them -> each texture's own bake (BakeAsset, the build's function, on the stock
// files) -> its mip bytes exactly as the build writes them into the pack, cut per mip for the GPU copy. Package recipes
// (bp_colour, material_colour, rich_row) change widget literals, not textures: Phase 4b. KB
// mods/menus-revamped/evidence/in-game-pack-builder-plan.md "Phase 4". Tests: tests/ingame_live_preview_tests.cpp, preview
// bytes vs the build on the game's assets: tests/ingame_pack_parity_tests.py (driver "preview" report).
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "builder/ColourModel.h"
#include "builder/PackBuild.h"

namespace builder::pack::live
{
	// the roles the live preview follows (4a: one role; 4b: every role of model::Roles())
	inline const std::vector<std::string>& LiveRoles()
	{
		static const std::vector<std::string> roles{ "lines" };
		return roles;
	}

	// the colour-file keys a role writes: model::Roles() members are pack value keys ("Skin.<name>" for [Skin])
	inline std::vector<std::string> RoleKeys(std::string_view a_role)
	{
		for (const auto& r : model::Roles())
			if (a_role == r.key) return r.members;
		return {};
	}

	inline std::vector<std::string> LiveKeys()
	{
		std::vector<std::string> keys;
		for (const auto& role : LiveRoles())
			for (auto& k : RoleKeys(role))
				if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(std::move(k));
		return keys;
	}

	// the keys whose value differs between two colour value sets, limited to a_scope (empty scope: every key)
	inline std::vector<std::string> ChangedKeys(const Values& a_before, const Values& a_after, const std::vector<std::string>& a_scope)
	{
		std::set<std::string> keys;
		for (const auto& [k, v] : a_before) keys.insert(k);
		for (const auto& [k, v] : a_after) keys.insert(k);
		std::vector<std::string> out;
		for (const auto& k : keys) {
			if (!a_scope.empty() && std::find(a_scope.begin(), a_scope.end(), k) == a_scope.end()) continue;
			const auto b = a_before.find(k), a = a_after.find(k);
			const bool same = b != a_before.end() && a != a_after.end() && b->second.n == a->second.n &&
			                  std::equal(b->second.v.begin(), b->second.v.begin() + b->second.n, a->second.v.begin());
			if (!same) out.push_back(k);
		}
		return out;
	}

	struct Target
	{
		std::string asset, recipe;   // the data row: stock asset (path under Content) and its recipe
	};

	// the data rows with a texture recipe that reads any of a_keys, in data order
	inline std::vector<Target> AffectedTextures(const PackData& a_data, const std::vector<std::string>& a_keys)
	{
		std::vector<Target> out;
		if (a_keys.empty()) return out;
		std::set<std::string> seen;
		for (const auto& [asset, recipe] : a_data.textures) {
			const auto info = Recipe(recipe, a_data);
			if (info.kind != RecipeKind::Texture || seen.count(asset)) continue;
			for (const auto& k : info.keys)
				if (std::find(a_keys.begin(), a_keys.end(), k) != a_keys.end()) {
					out.push_back({ asset, recipe });
					seen.insert(asset);
					break;
				}
		}
		return out;
	}

	// the UObject path of a stock asset: "Art/UI/Common/T_Line_V" -> "/Game/Art/UI/Common/T_Line_V.T_Line_V"
	inline std::string ObjectPath(const std::string& a_asset)
	{
		return "/Game/" + a_asset + "." + AssetName(a_asset);
	}

	// one mip of the body, top first, as the GPU copy needs it (block rows for BC formats)
	struct MipSlice
	{
		size_t offset = 0, size = 0;
		int    width = 0, height = 0;
		size_t rowBytes = 0;
		int    rows = 0;
	};

	// false when the body does not have exactly the size of a_mips mips of this format and size
	inline bool MipLayout(PixelFormat a_format, int a_width, int a_height, int a_mips, size_t a_bodySize, std::vector<MipSlice>& a_out)
	{
		a_out.clear();
		int block = 1, unit = 0;   // pixels per block side, bytes per block (or pixel)
		switch (a_format) {
		case PixelFormat::DXT1:     block = 4, unit = 8; break;
		case PixelFormat::DXT5:     block = 4, unit = 16; break;
		case PixelFormat::B8G8R8A8: unit = 4; break;
		case PixelFormat::G8:       unit = 1; break;
		default:                    return false;
		}
		if (a_width <= 0 || a_height <= 0 || a_mips <= 0 || a_mips > 16) return false;
		std::vector<MipSlice> slices;
		size_t                at = 0;
		for (int level = 0; level < a_mips; ++level) {
			MipSlice m;
			m.width = std::max(1, a_width >> level);
			m.height = std::max(1, a_height >> level);
			m.rowBytes = static_cast<size_t>((m.width + block - 1) / block) * static_cast<size_t>(unit);
			m.rows = (m.height + block - 1) / block;
			m.offset = at;
			m.size = m.rowBytes * static_cast<size_t>(m.rows);
			at += m.size;
			slices.push_back(m);
		}
		if (at != a_bodySize) return false;
		a_out = std::move(slices);
		return true;
	}

	// the DXGI formats (numeric DXGI_FORMAT) the RHI creates for a stock pixel format (KB dark-mode-ui.md "Loaded UI textures
	// in GPU memory": TYPELESS 76 / 70 / 90; the typed variants are accepted too)
	inline bool FormatMatches(PixelFormat a_format, unsigned a_dxgi)
	{
		switch (a_format) {
		case PixelFormat::DXT1:     return a_dxgi >= 70 && a_dxgi <= 72;                     // BC1_TYPELESS, _UNORM, _UNORM_SRGB
		case PixelFormat::DXT5:     return a_dxgi >= 76 && a_dxgi <= 78;                     // BC3
		case PixelFormat::B8G8R8A8: return a_dxgi == 87 || (a_dxgi >= 90 && a_dxgi <= 91);   // B8G8R8A8_UNORM, _TYPELESS, _UNORM_SRGB
		case PixelFormat::G8:       return a_dxgi >= 60 && a_dxgi <= 61;                     // R8_TYPELESS, _UNORM
		default:                    return false;
		}
	}

	struct Preview
	{
		std::string           asset;
		PixelFormat           format = PixelFormat::Unknown;
		int                   width = 0, height = 0;
		Bytes                 body;   // every mip, top first: the bytes the build writes into the pack's texture
		std::vector<MipSlice> mips;
	};

	// the build's bake of one texture row (BakeAsset without sizes), read back from the baked package
	inline bool BakePreview(const Target& a_target, const PackageFiles& a_stock, const Values& a_values, const PackData& a_data, const BakeInputs& a_inputs,
		Preview& a_out, std::string& a_reason)
	{
		a_out = {};
		if (Recipe(a_target.recipe, a_data).kind != RecipeKind::Texture) {
			a_reason = "recipe " + a_target.recipe + " changes no texture (widget colours: Phase 4b)";
			return false;
		}
		BakedAsset baked;
		if (BakeAsset(a_target.asset, a_target.recipe, a_stock, a_values, a_data, baked, a_reason, nullptr, a_inputs) != BakeResult::Baked) return false;
		const auto&    f = baked.files;
		TexturePackage tex;
		if (!ParseTexture(f.uasset, f.uexp, f.hasUbulk ? &f.ubulk : nullptr, tex, a_reason)) return false;
		Preview p{ a_target.asset, tex.format, tex.width, tex.height, GatherMips(tex, f.uexp, f.hasUbulk ? &f.ubulk : nullptr), {} };
		if (p.body.empty() || !MipLayout(p.format, p.width, p.height, static_cast<int>(tex.mips.size()), p.body.size(), p.mips)) {
			a_reason = "the baked texture's mips do not have the sizes of its format";
			return false;
		}
		a_out = std::move(p);
		return true;
	}

	// the stock cache's stamp: a cache extracted for another game build, builder, data or pak set is extracted again
	inline std::string CacheStamp(const std::vector<std::string>& a_names, const std::string& a_paks)
	{
		std::set<std::string> sorted(a_names.begin(), a_names.end());
		std::string           names;
		for (const auto& n : sorted) names += n + "\n";
		return std::string("game ") + kGameBuild + "\nbuilder " + kBuilderVersion + "\npaks " + a_paks + "\nnames " +
		       Sha256Hex(reinterpret_cast<const std::uint8_t*>(names.data()), names.size()) + "\n";
	}

	// latest wins: a worker takes only the newest request; older ones that were never taken are dropped
	template <class T> class LatestWins
	{
	public:
		void Post(T a_request)
		{
			std::scoped_lock guard(_lock);
			_next = std::move(a_request);
		}
		std::optional<T> Take()
		{
			std::scoped_lock guard(_lock);
			auto out = std::move(_next);
			_next.reset();
			return out;
		}
		bool Pending() const
		{
			std::scoped_lock guard(_lock);
			return _next.has_value();
		}

	private:
		mutable std::mutex _lock;
		std::optional<T>   _next;
	};
}
