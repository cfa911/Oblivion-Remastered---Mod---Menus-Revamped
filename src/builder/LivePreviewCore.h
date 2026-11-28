#pragma once
// In-game Pack Builder Phase 4a: the pure part of the live art preview (src/builder/LivePreview.cpp does the threads, the
// game and the GPU). A colour change in the window becomes: the changed colour-file keys (inside the live roles) -> the data
// rows whose texture recipe reads one of them -> each texture's own bake (BakeAsset, the build's function, on the stock
// files) -> its mip bytes exactly as the build writes them into the pack, cut per mip for the GPU copy. Package recipes
// (bp_colour, material_colour, rich_row) change widget literals, not textures: Phase 4b lists them (AffectedLiterals) with
// the four floats the bake writes (LiteralColour); src/builder/LiveWidgets.cpp puts them on the live widgets. KB
// mods/menus-revamped/evidence/in-game-pack-builder-plan.md "Phase 4". Tests: tests/ingame_live_preview_tests.cpp, preview
// bytes vs the build on the game's assets: tests/ingame_pack_parity_tests.py (driver "preview" report).
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
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
	// the roles the live preview follows: every role of model::Roles() (4a had "lines" only). The worker diffs every key of the
	// colour file (ChangedKeys with an empty scope), so the Advanced and Runtime elements outside the roles are live too.
	inline const std::vector<std::string>& LiveRoles()
	{
		static const std::vector<std::string> roles = [] {
			std::vector<std::string> out;
			for (const auto& r : model::Roles()) out.emplace_back(r.key);
			return out;
		}();
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

	// ---- Phase 4b: widget colour literals (bp_colour, material_colour, rich_row), the multi-mip window, re-paints ----

	enum class LiteralKind { Widget, Material, RichRow };

	// one colour literal a package recipe writes: who reads it in game and the bytes the bake patches
	struct LiveLiteral
	{
		LiteralKind           kind = LiteralKind::Widget;
		std::string           asset;    // the data row's stock asset (a widget blueprint, a material, DT_UI_MagicMenu)
		std::string           owner;    // the widget (export) name, the material parameter, or the rich text row
		std::string           colour;   // the colour-file key
		bool                  hasScale = false;
		double                scale = 1;
		std::array<double, 4> stock{};
		std::int64_t          offset = 0;
	};

	// the reader named by a data entry's label: a widget label's first word ("text (label)" -> "text"), a material label's
	// text before " (" ("BorderColor - Dark"); "" when the label names no parameter ("... (CachedExpressionData)")
	inline std::string OwnerName(LiteralKind a_kind, std::string_view a_label)
	{
		if (a_kind == LiteralKind::Material) {
			if (a_label.find("CachedExpressionData") != std::string_view::npos || a_label.find("shader map") != std::string_view::npos) return {};
			const auto note = a_label.find(" (");
			return std::string(a_label.substr(0, note));
		}
		const auto space = a_label.find(' ');
		return std::string(a_label.substr(0, space));
	}

	// the literals of the package rows (data order) whose colour is one of a_keys (constant colours never change: left out)
	inline std::vector<LiveLiteral> AffectedLiterals(const PackData& a_data, const std::vector<std::string>& a_keys)
	{
		std::vector<LiveLiteral> out;
		if (a_keys.empty()) return out;
		auto wanted = [&](const std::string& k) { return std::find(a_keys.begin(), a_keys.end(), k) != a_keys.end(); };
		std::set<std::string> seen;
		for (const auto& [asset, recipe] : a_data.textures) {
			const auto [kind, arg] = Partition(recipe);
			if (kind == "rich_row") {   // one data row per table row: every row of DT_UI_MagicMenu is its own literal
				const auto [row, colour] = Partition(arg);
				if (!seen.insert(asset + "|" + std::string(row)).second) continue;
				const auto table = a_data.richRows.find("DT_UI_MagicMenu");   // BakePackage's table
				if (table == a_data.richRows.end() || !wanted(std::string(colour))) continue;
				const auto entry = table->second.find(std::string(row));
				if (entry == table->second.end()) continue;
				LiveLiteral l;
				l.kind = LiteralKind::RichRow;
				l.asset = asset;
				l.owner = std::string(row);
				l.colour = std::string(colour);
				l.stock = entry->second.stock;
				l.offset = entry->second.offset;
				out.push_back(std::move(l));
			} else if ((kind == "bp_colour" || kind == "material_colour") && seen.insert(asset).second) {
				const auto bp = a_data.blueprints.find(std::string(arg));
				if (bp == a_data.blueprints.end()) continue;
				const auto lk = kind == "bp_colour" ? LiteralKind::Widget : LiteralKind::Material;
				for (const auto& c : bp->second) {
					model::Value constant;
					if (recipes::ConstantColour(c.colour, constant) || !wanted(c.colour)) continue;
					LiveLiteral l;
					l.kind = lk;
					l.asset = asset;
					l.owner = OwnerName(lk, c.label);
					l.colour = c.colour;
					l.hasScale = c.hasScale;
					l.scale = c.scale;
					l.stock = c.stock;
					l.offset = c.offset;
					out.push_back(std::move(l));
				}
			}
		}
		return out;
	}

	// the stock literal at its offset in the stock .uexp, refused like the bake refuses (out of range, not the stock value)
	inline bool StockLiteral(const LiveLiteral& a_lit, const Bytes& a_uexp, std::array<float, 4>& a_out)
	{
		if (a_lit.offset < 0 || static_cast<std::uint64_t>(a_lit.offset) + 16 > a_uexp.size()) return false;
		std::array<float, 4> f{};
		std::memcpy(f.data(), a_uexp.data() + a_lit.offset, 16);
		for (size_t i = 0; i < 4; ++i)
			if (std::fabs(static_cast<double>(f[i]) - a_lit.stock[i]) > 5e-4) return false;   // BakePackage's "differs"
		a_out = f;
		return true;
	}

	// the four floats the bake writes for a literal with these values (a_stockAlpha: the stock literal's alpha, kept by
	// bp_colour / material_colour; rich_row writes alpha 1)
	inline bool LiteralColour(const LiveLiteral& a_lit, const Values& a_values, float a_stockAlpha, std::array<float, 4>& a_out)
	{
		model::Value value;
		if (!recipes::ConstantColour(a_lit.colour, value)) {
			const auto it = a_values.find(a_lit.colour);
			if (it == a_values.end()) return false;
			value = it->second;
		}
		if (a_lit.kind == LiteralKind::RichRow) {   // BakePackage rich_row: linear RGB, alpha 1
			a_out = { static_cast<float>(model::Linear(value.v[0])), static_cast<float>(model::Linear(value.v[1])), static_cast<float>(model::Linear(value.v[2])), 1.0f };
			return true;
		}
		if (a_lit.hasScale) {   // BakePackage bp_colour / material_colour, in its order
			for (int i = 0; i < 3; ++i) value.v[static_cast<size_t>(i)] = std::min(255.0, value.v[static_cast<size_t>(i)] * a_lit.scale);
			value.n = 3;
		}
		const auto rgba = recipes::LiteralRgba(value);
		a_out = { static_cast<float>(rgba[0]), static_cast<float>(rgba[1]), static_cast<float>(rgba[2]), a_stockAlpha };
		return true;
	}

	// the blueprint class of a widget blueprint asset: "UI/X/WBP_A" -> "/Game/UI/X/WBP_A.WBP_A_C"
	inline std::string ClassPath(const std::string& a_asset)
	{
		return "/Game/" + a_asset + "." + AssetName(a_asset) + "_C";
	}

	// same colour within the bake's tolerance (5e-4 per channel)
	inline bool SameColour(const float* a_a, const std::array<float, 4>& a_b)
	{
		for (size_t i = 0; i < 4; ++i)
			if (!(std::fabs(a_a[i] - a_b[i]) <= 5e-4f)) return false;   // a NaN never matches
		return true;
	}

	// the 4-byte aligned offsets in an object's bytes where one of a_candidates sits (every channel within 5e-4)
	inline std::vector<size_t> ColourSlots(const std::uint8_t* a_bytes, size_t a_size, const std::vector<std::array<float, 4>>& a_candidates)
	{
		std::vector<size_t> out;
		for (size_t at = 0; at + 16 <= a_size; at += 4) {
			float f[4];
			std::memcpy(f, a_bytes + at, 16);
			for (const auto& c : a_candidates)
				if (SameColour(f, c)) {
					out.push_back(at);
					break;
				}
		}
		return out;
	}

	// the colours the screen shows before any edit: the colour file only when the Custom pack is installed, else Dark's
	// (Classic shows stock, which every paint knows anyway). RMI-115: with Dark installed the file's colours were taken, so
	// the real colour slot (Dark's white) was not recognised and the paint went to a stale copy of the stock colour
	// With Custom installed the screen shows what the pack was built from (its manifest's "style"), which can differ from the
	// saved file (colours saved but not rebuilt): those values win; keys the manifest does not list keep the file's value.
	inline Values ShownValues(bool a_usesCustom, const Values& a_file, const Values& a_dark, const json::Value* a_customStyle)
	{
		if (!a_usesCustom) return a_dark;
		Values out = a_file;
		if (!a_customStyle) return out;
		for (auto& [key, value] : out) {
			const json::Value* baked = a_customStyle->Find(key);
			if (!baked) continue;
			model::Value v{};
			if (baked->type == json::Type::Array) {
				if (baked->items.empty() || baked->items.size() > 4) continue;
				v.n = static_cast<int>(baked->items.size());
				for (size_t i = 0; i < baked->items.size(); ++i) v.v[i] = baked->items[i].Number();
			} else if (baked->type == json::Type::Int || baked->type == json::Type::Float) {
				v.n = 1;
				v.v[0] = baked->Number();
			} else continue;
			value = v;
		}
		return out;
	}

	// the setter LiveWidgets calls for a colour property, so the game redraws the widget at once ("" when it has none).
	// "Color" is the game's text prefab WBP_AltarTextBlock: its own variable (+0x2F8) and SetColor, which passes the colour
	// to the inner text (docs/alchemy/probe-runs.md); the bake writes the prefab's literal into that variable.
	inline std::string SetterFor(std::string_view a_property)
	{
		if (a_property == "Color") return "SetColor";
		if (a_property == "ColorAndOpacity") return "SetColorAndOpacity";
		if (a_property == "BrushColor") return "SetBrushColor";
		if (a_property == "ContentColorAndOpacity") return "SetContentColorAndOpacity";
		if (a_property == "Brush") return "SetBrushTintColor";
		return {};
	}

	// whether a live widget gets the new colour: a prefab always (its slot is known by name, and only colours edited this
	// session are posted), other widgets only when they hold a known colour (a colour the game set itself is left alone)
	inline bool ShouldPaint(bool a_prefab, bool a_alreadyWanted, bool a_holdsKnown)
	{
		return !a_alreadyWanted && (a_prefab || a_holdsKnown);
	}

	// a reflected field of a widget class (or its parents): name, offset, size
	struct FieldSpan
	{
		std::string   name;
		std::int32_t  offset = 0;
		std::int32_t  size = 0;
	};

	// the property with a setter that LiveWidgets calls (so Slate redraws the widget at once)
	inline bool IsColourProperty(std::string_view a_name)
	{
		return a_name == "ColorAndOpacity" || a_name == "BrushColor" || a_name == "ContentColorAndOpacity" || a_name == "Brush";
	}

	// the property that holds a colour slot: a colour property when one covers it (RMI-115: a TextBlock's field list also
	// names ColorAndOpacityDelegate over ColorAndOpacity's bytes, which has no setter, so rows were written without a
	// redraw), else the first field that holds it
	inline std::string PropertyFor(const std::vector<FieldSpan>& a_fields, std::int32_t a_offset)
	{
		std::string first;
		for (const auto& f : a_fields)
			if (f.size > 0 && a_offset >= f.offset && a_offset < f.offset + f.size) {
				if (IsColourProperty(f.name)) return f.name;
				if (first.empty()) first = f.name;
			}
		return first;
	}

	// a streamed texture's GPU resource may hold only the lower mips: the package mip whose size is the resource's top mip
	// (-1 when none fits) and how many mips to copy (the resource's, at most what the package has below it)
	inline int MipWindow(int a_width, int a_height, int a_mips, int a_gpuWidth, int a_gpuHeight, int a_gpuMips, int& a_count)
	{
		a_count = 0;
		if (a_width <= 0 || a_height <= 0 || a_gpuMips <= 0) return -1;
		for (int level = 0; level < a_mips; ++level)
			if (std::max(1, a_width >> level) == a_gpuWidth && std::max(1, a_height >> level) == a_gpuHeight) {
				a_count = std::min(a_gpuMips, a_mips - level);
				return level;
			}
		return -1;
	}

	// what a texture was last painted with: a menu opened later loads the pak's art into a new resource, which is painted again
	struct PaintMemo
	{
		const void*   resource = nullptr;
		std::uint64_t version = 0;
	};
	inline bool NeedsPaint(const PaintMemo& a_memo, const void* a_resource, std::uint64_t a_version)
	{
		return a_resource && (a_memo.resource != a_resource || a_memo.version != a_version);
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
