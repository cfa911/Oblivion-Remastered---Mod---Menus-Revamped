#pragma once
// In-game Pack Builder (Phase 3a): the recipes of tools/pack_builder/pipeline.py ported to C++, one family at a time.
// 3a: the recipes that need no image library (texture block/endpoint maps: flat, flat_white, clear, grey_scale, skin_flat,
// tone, neutral, laurels, panel, altar_metal; package literal patches: bp_colour, material_colour, rich_row). Every other kind
// answers RecipeKind::Later until its phase ports it. Python's arithmetic order and round() (half to even) are kept, so the
// results match byte for byte: tests/ingame_pack_core_tests.cpp, tests/ingame_pack_parity_tests.py.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "builder/PackData.h"
#include "builder/PackTexture.h"

namespace builder::pack
{
	enum class RecipeKind { Texture, Package, Later, Unknown };

	struct RecipeInfo
	{
		RecipeKind               kind = RecipeKind::Unknown;
		std::vector<std::string> keys;   // the colour-file values it reads, in pipeline.recipe() order (floats, then colours)
		std::string              error;  // Unknown: the KeyError text ("KeyError: 'name'")
	};

	struct Dds
	{
		PixelFormat format = PixelFormat::Unknown;
		int         width = 0, height = 0, mips = 0;
		Bytes       body;   // every mip, top first (the DDS file after its header)
	};

	namespace recipes
	{
		using Colour = std::array<double, 3>;

		inline double Round(double a_v) { return model::PyRound(a_v); }
		inline double Lum(const Colour& a_c) { return model::Lum(a_c); }
		inline double Srgb(double a_l) { return Round(model::SrgbExact(a_l)); }   // pipeline.srgb

		inline std::uint16_t U16(const Bytes& a_b, size_t a_at) { return static_cast<std::uint16_t>(a_b[a_at] | (a_b[a_at + 1] << 8)); }
		inline void          Put16(Bytes& a_b, size_t a_at, std::uint32_t a_v)
		{
			a_b[a_at] = static_cast<std::uint8_t>(a_v & 0xFF);
			a_b[a_at + 1] = static_cast<std::uint8_t>((a_v >> 8) & 0xFF);
		}
		inline std::uint32_t U32(const Bytes& a_b, size_t a_at) { return a_b[a_at] | (a_b[a_at + 1] << 8) | (a_b[a_at + 2] << 16) | (std::uint32_t(a_b[a_at + 3]) << 24); }
		inline void          Put32(Bytes& a_b, size_t a_at, std::uint32_t a_v)
		{
			for (int i = 0; i < 4; ++i) a_b[a_at + static_cast<size_t>(i)] = static_cast<std::uint8_t>((a_v >> (8 * i)) & 0xFF);
		}

		inline Colour RgbOf(std::uint32_t a_e)   // pipeline._rgb_of: a 565 endpoint as 0-255 floats
		{
			return { static_cast<double>(((a_e >> 11) & 31) * 255) / 31, static_cast<double>(((a_e >> 5) & 63) * 255) / 63,
				static_cast<double>((a_e & 31) * 255) / 31 };
		}

		inline std::uint32_t Rgb565(const Colour& a_rgb)
		{
			const double r = Round(a_rgb[0]), g = Round(a_rgb[1]), b = Round(a_rgb[2]);
			return (static_cast<std::uint32_t>(Round(r * 31 / 255)) << 11) | (static_cast<std::uint32_t>(Round(g * 63 / 255)) << 5) |
			       static_cast<std::uint32_t>(Round(b * 31 / 255));
		}

		inline std::uint32_t Gray565(double a_v)
		{
			const auto k = static_cast<std::uint32_t>(std::max(0.0, std::min(31.0, Round(a_v * 31 / 255))));
			return (k << 11) | ((2 * k) << 5) | k;
		}

		inline const std::vector<std::uint32_t>& GreyLevels()   // pipeline._GREY_LEVELS
		{
			static const std::vector<std::uint32_t> levels = [] {
				std::vector<std::uint32_t> out;
				for (int v = 0; v < 256; ++v) out.push_back(Gray565(v));
				std::sort(out.begin(), out.end());
				out.erase(std::unique(out.begin(), out.end()), out.end());
				return out;
			}();
			return levels;
		}

		inline std::uint32_t FlipTransparentBits(std::uint32_t a_bits)   // the BC1 three-colour mode with its endpoints swapped
		{
			std::uint32_t out = 0;
			for (int i = 0; i < 16; ++i) {
				std::uint32_t v = (a_bits >> (2 * i)) & 3;
				if (v < 2) v ^= 1;
				out |= v << (2 * i);
			}
			return out;
		}

		inline double Clamp255(double a_v) { return std::max(0.0, std::min(255.0, Round(a_v))); }

		// pipeline.map_colours: every colour through a_fn (BC3/BC1 endpoints, indices and alpha kept; 32-bit per pixel)
		template <class F> Bytes MapColours(PixelFormat a_format, const Bytes& a_body, F a_fn)
		{
			Bytes out = a_body;
			if (a_format == PixelFormat::B8G8R8A8) {
				for (size_t at = 0; at + 3 < out.size(); at += 4) {
					const Colour c = a_fn(Colour{ static_cast<double>(out[at + 2]), static_cast<double>(out[at + 1]), static_cast<double>(out[at]) });
					out[at + 2] = static_cast<std::uint8_t>(Clamp255(c[0]));
					out[at + 1] = static_cast<std::uint8_t>(Clamp255(c[1]));
					out[at] = static_cast<std::uint8_t>(Clamp255(c[2]));
				}
				return out;
			}
			auto mapped = [&](std::uint32_t e) {
				const Colour c = a_fn(RgbOf(e));
				return Rgb565({ Clamp255(c[0]), Clamp255(c[1]), Clamp255(c[2]) });
			};
			if (a_format == PixelFormat::DXT5) {
				for (size_t offset = 8; offset < out.size(); offset += 16)
					for (size_t at : { offset, offset + 2 }) Put16(out, at, mapped(U16(out, at)));
				return out;
			}
			if (a_format != PixelFormat::DXT1) return {};
			for (size_t at = 0; at + 7 < out.size(); at += 8) {
				const std::uint32_t c0 = U16(out, at), c1 = U16(out, at + 2);
				std::uint32_t       n0 = mapped(c0), n1 = mapped(c1), bits = U32(out, at + 4);
				if (c0 > c1 && n0 <= n1) {
					if (n0 == n1) bits = 0;
					else {
						std::swap(n0, n1);
						bits ^= 0x55555555u;
					}
				} else if (c0 <= c1 && n0 > n1) {
					std::swap(n0, n1);
					bits = FlipTransparentBits(bits);
				}
				Put16(out, at, n0);
				Put16(out, at + 2, n1);
				Put32(out, at + 4, bits);
			}
			return out;
		}

		inline Bytes FlatBlocks(PixelFormat a_format, const Bytes& a_body, const Colour& a_rgb)   // pipeline.flat_blocks
		{
			if (a_format == PixelFormat::B8G8R8A8) return MapColours(a_format, a_body, [&](const Colour&) { return a_rgb; });
			if (a_format != PixelFormat::DXT5) return {};
			Bytes      out = a_body;
			const auto colour = Rgb565(a_rgb);
			for (size_t offset = 0; offset + 16 <= out.size(); offset += 16) {
				Put16(out, offset + 8, colour);
				Put16(out, offset + 10, colour);
				Put32(out, offset + 12, 0);
			}
			return out;
		}

		inline Bytes ClearAlpha(const Bytes& a_body)
		{
			Bytes out = a_body;
			for (size_t offset = 0; offset + 8 <= out.size(); offset += 16) std::memset(out.data() + offset, 0, 8);
			return out;
		}

		inline std::array<int, 8> AlphaLevels(int a_first, int a_second)
		{
			std::array<int, 8> l{ a_first, a_second };
			if (a_first > a_second) {
				for (int i = 1; i < 7; ++i) l[static_cast<size_t>(i + 1)] = ((7 - i) * a_first + i * a_second) / 7;
			} else {
				for (int i = 1; i < 5; ++i) l[static_cast<size_t>(i + 1)] = ((5 - i) * a_first + i * a_second) / 5;
				l[6] = 0;
				l[7] = 255;
			}
			return l;
		}

		inline void ScaleBc3Alpha(std::uint8_t* a_block, double a_opacity)   // pipeline.scale_bc3_alpha
		{
			const auto levels = AlphaLevels(a_block[0], a_block[1]);
			int        scaled[8];
			for (int i = 0; i < 8; ++i) scaled[i] = static_cast<int>(Round(levels[static_cast<size_t>(i)] * a_opacity));
			std::uint64_t bits = 0;
			for (int i = 0; i < 6; ++i) bits |= std::uint64_t(a_block[2 + i]) << (8 * i);
			int pixels[16];
			for (int i = 0; i < 16; ++i) pixels[i] = scaled[(bits >> (3 * i)) & 7];
			const int first = *std::max_element(pixels, pixels + 16), second = *std::min_element(pixels, pixels + 16);
			a_block[0] = static_cast<std::uint8_t>(first);
			a_block[1] = static_cast<std::uint8_t>(second);
			std::uint64_t result = 0;
			if (first != second) {
				const auto l = AlphaLevels(first, second);
				for (int i = 0; i < 16; ++i) {
					int best = 0;
					for (int k = 1; k < 8; ++k)
						if (std::abs(l[static_cast<size_t>(k)] - pixels[i]) < std::abs(l[static_cast<size_t>(best)] - pixels[i])) best = k;
					result |= std::uint64_t(best) << (3 * i);
				}
			}
			for (int i = 0; i < 6; ++i) a_block[2 + i] = static_cast<std::uint8_t>((result >> (8 * i)) & 0xFF);
		}

		inline Bytes Laurels(const Bytes& a_body, Colour a_rgb, const model::Value& a_tint)   // pipeline.laurels
		{
			if (!(a_tint.v[0] == 1 && a_tint.v[1] == 1 && a_tint.v[2] == 1)) {
				const auto     base = Rgb565(a_rgb);
				const unsigned channels[3] = { (base >> 11) & 31, (base >> 5) & 63, base & 31 }, levels[3] = { 31, 63, 31 };
				for (int i = 0; i < 3; ++i)
					a_rgb[static_cast<size_t>(i)] = Srgb(model::Linear(static_cast<double>(channels[i] * 255) / levels[i]) * a_tint.v[static_cast<size_t>(i)]);
			}
			Bytes baked = FlatBlocks(PixelFormat::DXT5, a_body, a_rgb);
			if (a_tint.v[3] != 1)
				for (size_t offset = 0; offset + 8 <= baked.size(); offset += 16) ScaleBc3Alpha(baked.data() + offset, a_tint.v[3]);
			return baked;
		}

		template <class F> Bytes EndpointCurve(const Bytes& a_body, F a_gray)   // pipeline.endpoint_curve (BC3)
		{
			Bytes out = a_body;
			for (size_t offset = 8; offset < out.size(); offset += 16)
				for (size_t at : { offset, offset + 2 }) {
					const double v = a_gray(Lum(RgbOf(U16(out, at))));
					Put16(out, at, Rgb565({ v, v, v }));
				}
			return out;
		}

		inline double PanelGray(double a_v, double a_frame, double a_panel)   // pipeline.panel_gray (PANEL_DARK 42, PANEL_LIGHT 116)
		{
			return a_frame + Round((a_panel - a_frame) * std::min(1.0, std::max(0.0, (a_v - 42) / (116 - 42))));
		}

		inline Colour AltarMetal(const Colour& a_rgb, const Colour& a_metal)   // ALTAR_REF 150, ALTAR_CONTRAST 0.8
		{
			const double k = Lum(a_rgb) > 0 ? std::pow(Lum(a_rgb) / 150, 0.8) : 0.0;
			return { a_metal[0] * k, a_metal[1] * k, a_metal[2] * k };
		}

		inline Bytes Tone(const Bytes& a_body, const Colour& a_rgb)
		{
			Bytes  out = a_body;
			double peak = 0;
			bool   any = false;
			for (size_t offset = 8; offset < out.size(); offset += 16)
				for (size_t at : { offset, offset + 2 }) {
					const double l = Lum(RgbOf(U16(out, at)));
					peak = any ? std::max(peak, l) : l;
					any = true;
				}
			if (!any) peak = 255;
			for (size_t offset = 8; offset < out.size(); offset += 16)
				for (size_t at : { offset, offset + 2 }) {
					const double v = Lum(RgbOf(U16(out, at)));
					const double k = peak > 0 ? std::min(1.0, v / peak) : 0.0;
					Put16(out, at, Rgb565({ Round(a_rgb[0] * k), Round(a_rgb[1] * k), Round(a_rgb[2] * k) }));
				}
			return out;
		}

		inline Bytes Neutral(const Bytes& a_body, const Colour& a_rgb, double a_body_lum)
		{
			Bytes        out = a_body;
			const double target = Lum(a_rgb);
			for (size_t offset = 8; offset < out.size(); offset += 16)
				for (size_t at : { offset, offset + 2 }) {
					const double grey = std::min(255.0, Lum(RgbOf(U16(out, at))) * target / a_body_lum);
					Put16(out, at, Rgb565({ grey, grey, grey }));
				}
			return out;
		}

		template <class F> Bytes GreyBlocks(PixelFormat a_format, const Bytes& a_body, F a_grey)   // pipeline.grey_blocks
		{
			if (a_format != PixelFormat::DXT1 && a_format != PixelFormat::DXT5) return {};
			Bytes        out = a_body;
			const bool   bc1 = a_format == PixelFormat::DXT1;
			const size_t stride = bc1 ? 8 : 16, first = bc1 ? 0 : 8;
			const auto&  levels = GreyLevels();
			auto         lumOf = [](std::uint32_t e) { return Lum(RgbOf(e)); };
			for (size_t at = first; at + 7 < out.size(); at += stride) {
				const std::uint32_t c0 = U16(out, at), c1 = U16(out, at + 2);
				std::uint32_t       bits = U32(out, at + 4);
				if (bc1 && c0 <= c1) {
					std::uint32_t n0 = Gray565(a_grey(lumOf(c0))), n1 = Gray565(a_grey(lumOf(c1)));
					if (n0 > n1) {
						std::swap(n0, n1);
						bits = FlipTransparentBits(bits);
					}
					Put16(out, at, n0);
					Put16(out, at + 2, n1);
					Put32(out, at + 4, bits);
					continue;
				}
				const double l0 = lumOf(c0), l1 = lumOf(c1);
				const double palette[4] = { l0, l1, (2 * l0 + l1) / 3, (l0 + 2 * l1) / 3 };
				double       targets[16];
				for (int i = 0; i < 16; ++i) targets[i] = a_grey(palette[(bits >> (2 * i)) & 3]);
				const double  loT = *std::min_element(targets, targets + 16), hiT = *std::max_element(targets, targets + 16);
				std::uint32_t lo = levels.front(), hi = levels.back();
				bool          haveLo = false, haveHi = false;
				for (auto c : levels) {
					if (lumOf(c) <= loT + 1e-6 && (!haveLo || c > lo)) {
						lo = c;
						haveLo = true;
					}
					if (lumOf(c) >= hiT - 1e-6 && (!haveHi || c < hi)) {
						hi = c;
						haveHi = true;
					}
				}
				if (!haveLo) lo = levels.front();
				if (!haveHi) hi = levels.back();
				if (hi <= lo) {
					const size_t i = static_cast<size_t>(std::find(levels.begin(), levels.end(), lo) - levels.begin());
					if (i + 1 < levels.size()) hi = levels[i + 1];
					else {
						hi = lo;
						lo = levels[i - 1];
					}
				}
				const double a = lumOf(hi), b = lumOf(lo);
				const double steps[4] = { a, b, (2 * a + b) / 3, (a + 2 * b) / 3 };
				std::uint32_t newBits = 0;
				for (int i = 0; i < 16; ++i) {
					int best = 0;
					for (int k = 1; k < 4; ++k)
						if (std::fabs(steps[k] - targets[i]) < std::fabs(steps[best] - targets[i])) best = k;
					newBits |= static_cast<std::uint32_t>(best) << (2 * i);
				}
				Put16(out, at, hi);
				Put16(out, at + 2, lo);
				Put32(out, at + 4, newBits);
			}
			return out;
		}

		inline Bytes GreyScale(PixelFormat a_format, const Bytes& a_body, double a_k)
		{
			if (a_format == PixelFormat::B8G8R8A8)
				return MapColours(a_format, a_body, [&](const Colour& c) {
					const double g = std::min(255.0, Lum(c) * a_k);
					return Colour{ g, g, g };
				});
			return GreyBlocks(a_format, a_body, [&](double v) { return std::min(255.0, v * a_k); });
		}

		inline std::array<double, 4> LiteralRgba(const model::Value& a_v)   // pipeline.literal_rgba
		{
			if (a_v.n == 1) return { a_v.v[0], a_v.v[0], a_v.v[0], 1.0 };
			if (a_v.n == 4) return { a_v.v[0], a_v.v[1], a_v.v[2], a_v.v[3] };
			return { model::Linear(a_v.v[0]), model::Linear(a_v.v[1]), model::Linear(a_v.v[2]), 1.0 };
		}

		inline bool ConstantColour(std::string_view a_name, model::Value& a_out)   // pipeline.CONSTANT_COLOURS
		{
			a_out.n = 3;
			if (a_name == "white") a_out.v = { 255.0, 255.0, 255.0, 0 };
			else if (a_name == "statusRed") a_out.v = { 205.0, 62.0, 62.0, 0 };
			else if (a_name == "steelGlow") a_out.v = { 193.0, 215.0, 255.0, 0 };
			else return false;
			return true;
		}

		inline double ParseFloat(std::string_view a_s)
		{
			double v = 0;
			std::from_chars(a_s.data(), a_s.data() + a_s.size(), v);
			return v;
		}

		inline std::string Repr4(const float* a_f)   // a Python tuple of four floats, for the skip messages
		{
			std::string out = "(";
			for (int i = 0; i < 4; ++i) out += (i ? ", " : "") + json::PyRepr(a_f[i]);
			return out + ")";
		}
		inline std::string Repr4(const std::array<double, 4>& a_d)
		{
			std::string out = "(";
			for (int i = 0; i < 4; ++i) out += (i ? ", " : "") + json::PyRepr(a_d[static_cast<size_t>(i)]);
			return out + ")";
		}
	}

	inline std::pair<std::string_view, std::string_view> Partition(std::string_view a_s)   // str.partition(":") (head, rest)
	{
		const auto colon = a_s.find(':');
		if (colon == std::string_view::npos) return { a_s, {} };
		return { a_s.substr(0, colon), a_s.substr(colon + 1) };
	}

	inline RecipeInfo Recipe(std::string_view a_name, const PackData& a_data)
	{
		const auto [kind, arg] = Partition(a_name);
		RecipeInfo info;
		auto texture = [&](std::vector<std::string> keys) {
			info.kind = RecipeKind::Texture;
			info.keys = std::move(keys);
			return info;
		};
		if (kind == "flat") return texture({ std::string(arg) });
		if (a_name == "flat_white" || a_name == "clear" || kind == "grey_scale") return texture({});
		if (kind == "skin_flat") return texture({ "Skin." + std::string(Partition(arg).first) });
		if (kind == "tone" || kind == "altar_metal") return texture({ std::string(arg) });
		if (kind == "neutral") return texture({ std::string(Partition(arg).first) });
		if (a_name == "laurels") return texture({ "pattern", "Skin.pattern" });
		if (a_name == "panel") return texture({ "frame", "panel" });
		if (kind == "bp_colour" || kind == "material_colour") {
			const auto it = a_data.blueprints.find(std::string(arg));
			if (it == a_data.blueprints.end()) {
				info.error = "KeyError: '" + std::string(arg) + "'";
				return info;
			}
			std::set<std::string> names;
			model::Value          unused;
			for (const auto& c : it->second)
				if (!recipes::ConstantColour(c.colour, unused)) names.insert(c.colour);
			info.kind = RecipeKind::Package;
			info.keys.assign(names.begin(), names.end());
			return info;
		}
		if (kind == "rich_row") {
			info.kind = RecipeKind::Package;
			info.keys = { std::string(Partition(arg).second) };
			return info;
		}
		// Phase 3c: the image recipes (PackRecipesImage.h), keys in pipeline.recipe() order
		const std::string a(arg);
		if (kind == "paper_scale" || kind == "popup_panel") return texture({ "parchmentLight" });
		if (kind == "popup_paper") {
			if (!arg.empty() && recipes::ParseFloat(arg) == 0) return texture({ "parchmentDark", "parchmentLight" });
			return texture({ "parchmentDark", "parchmentLight", "pattern" });
		}
		if (kind == "mask_peak" || kind == "gray_smooth" || kind == "charcoal" || kind == "skill_card") return texture({});
		if (kind == "smooth_grain" || kind == "paper_invert") return texture({ "parchmentDark", "parchmentLight", "panel" });
		if (kind == "page_grain" || kind == "page_invert") return texture({ "frame", "panel", "parchmentDark", "parchmentLight" });
		if (kind == "details_invert" || kind == "details_smooth") return texture({ "detailsPage" });
		if (kind == "band_lines" || kind == "line_tint") return texture({ "pattern" });
		if (kind == "laurel_tint") {
			const auto key = Partition(arg).first;
			return texture({ key.empty() ? std::string("pattern") : std::string(key) });
		}
		if (kind == "charcoal_paper") {
			if (arg != "enchanting" && arg != "quests") {
				info.error = "no page texture for charcoal_paper preset '" + a + "'";
				return info;
			}
			return texture({ "panel", "parchmentDark", "parchmentLight" });
		}
		if (kind == "quest_icon") return texture({ "menuTitle", "effectIcon" });
		if (a_name == "ink_gold") return texture({ "effectIcon", "frame" });
		if (a_name == "white_backing" || a_name == "skill_ring") return texture({});
		if (kind == "skill_icon") return texture({ "frame" });
		if (kind == "flat_alpha") return texture({ std::string(Partition(arg).first) });
		if (kind == "tab" || kind == "magic_tab") return texture({ "selected", "unselected", "gap", "outline", "glyph", "glyphDim" });
		if (kind == "frame_metal") return texture({ a });
		if (kind == "altar_panel_invert") return texture({ std::string(Partition(arg).first), "panel", "parchmentDark", "parchmentLight" });
		if (kind == "altar_panel") return texture({ a, "altarPaper" });
		if (a_name == "parchment:enchanting" || a_name == "parchment") return texture({ "parchmentDark", "parchmentLight" });
		if (a_name == "details_page") return texture({ "detailsPage" });
		if (a_name == "alchemy") return texture({ "alchemyMetal", "alchemyStone" });
		if (a_name == "tab_strip") return texture({ "enchantingTabIcon", "enchantingTabPaper" });
		if (a_name == "tab_paper") return texture({ "tabPaperDark", "tabPaperLight", "tabGlyph" });
		if (a_name == "png" || a_name == "gray") return texture({});
		if (a_name == "enchanted_dot") return texture({ "enchantedDot" });
		if (a_name == "stolen_mark") return texture({ "stolenMark" });
		if (a_name == "preview_frame") return texture({ "previewFrameLight", "previewFrameDark" });
		info.error = "KeyError: '" + std::string(a_name) + "'";
		return info;
	}

	// pipeline.format_supported: which stock formats a recipe kind can recolour
	inline bool FormatSupported(std::string_view a_kind, PixelFormat a_format)
	{
		using P = PixelFormat;
		if (a_kind == "skill_icon") return a_format == P::DXT1;
		if ((a_kind == "charcoal" || a_kind == "charcoal_paper") && a_format == P::DXT1) return true;
		if (a_format == P::DXT5) return true;
		if (a_kind == "paper_scale" && a_format == P::G8) return true;
		static constexpr std::string_view bc1OrRaw[] = { "parchment", "paper_scale", "mask_peak", "laurel_tint", "altar_metal", "altar_panel",
			"altar_panel_invert", "tab_strip", "grey_scale" };
		for (auto k : bc1OrRaw)
			if (a_kind == k) return a_format == P::DXT1 || a_format == P::B8G8R8A8;
		if (a_kind == "quest_icon" || a_kind == "ink_gold" || a_kind == "white_backing") return a_format == P::B8G8R8A8;
		return (a_kind == "flat" || a_kind == "skin_flat" || a_kind == "flat_white") && a_format == P::B8G8R8A8;
	}

	inline bool BakeTexture(std::string_view a_recipe, const Dds& a_stock, const Values& a_values, Bytes& a_out, std::string& a_skip)
	{
		using namespace recipes;
		using recipes::Colour;   // not PackImage.h's pixel Colour
		const auto [kind, arg] = Partition(a_recipe);
		auto value = [&](const std::string& k) -> const model::Value* {
			const auto it = a_values.find(k);
			return it == a_values.end() ? nullptr : &it->second;
		};
		auto rgb = [](const model::Value& v) { return Colour{ v.v[0], v.v[1], v.v[2] }; };
		const auto info = Recipe(a_recipe, PackData{});
		for (const auto& k : info.keys)
			if (!value(k)) {
				a_skip = "KeyError: '" + k + "'";
				return false;
			}
		const auto& body = a_stock.body;
		const auto  fmt = a_stock.format;
		Bytes       out;
		if (kind == "flat") out = FlatBlocks(fmt, body, rgb(*value(std::string(arg))));
		else if (a_recipe == "flat_white") out = FlatBlocks(fmt, body, { 255.0, 255.0, 255.0 });
		else if (a_recipe == "clear") out = fmt == PixelFormat::DXT5 ? ClearAlpha(body) : Bytes{};
		else if (kind == "grey_scale") out = GreyScale(fmt, body, ParseFloat(arg));
		else if (kind == "skin_flat") {
			const auto [key, base] = Partition(arg);
			const auto& tint = *value("Skin." + std::string(key));
			const double b = model::Linear(ParseFloat(base));
			out = FlatBlocks(fmt, body, { Srgb(b * tint.v[0]), Srgb(b * tint.v[1]), Srgb(b * tint.v[2]) });
		} else if (kind == "tone") out = fmt == PixelFormat::DXT5 ? Tone(body, rgb(*value(std::string(arg)))) : Bytes{};
		else if (kind == "neutral") {
			const auto [key, lum] = Partition(arg);
			out = fmt == PixelFormat::DXT5 ? Neutral(body, rgb(*value(std::string(key))), ParseFloat(lum)) : Bytes{};
		} else if (a_recipe == "laurels") out = fmt == PixelFormat::DXT5 ? Laurels(body, rgb(*value("pattern")), *value("Skin.pattern")) : Bytes{};
		else if (a_recipe == "panel") {
			const double frame = value("frame")->v[0], panel = value("panel")->v[0];
			out = fmt == PixelFormat::DXT5 ? EndpointCurve(body, [&](double b) { return PanelGray(b, frame, panel); }) : Bytes{};
		} else if (kind == "altar_metal") {
			const Colour metal = rgb(*value(std::string(arg)));
			out = MapColours(fmt, body, [&](const Colour& c) { return AltarMetal(c, metal); });
		} else {
			a_skip = "recipe " + std::string(a_recipe) + " is not ported yet";
			return false;
		}
		if (out.empty() && !body.empty()) {
			a_skip = "AssertionError: not BC3";
			return false;
		}
		if (out.size() != body.size()) {
			a_skip = "the recipe's result has another size (" + std::to_string(out.size()) + " bytes, stock texture " + std::to_string(body.size()) + ")";
			return false;
		}
		a_out = std::move(out);
		return true;
	}

	inline bool BakePackage(std::string_view a_recipe, const std::string& a_package, const Bytes& a_uexp, const Values& a_values,
		const PackData& a_data, Bytes& a_out, std::string& a_skip)
	{
		using namespace recipes;
		const auto [kind, arg] = Partition(a_recipe);
		auto read4 = [&](std::int64_t at, float* f) {
			if (at < 0 || static_cast<std::uint64_t>(at) + 16 > a_uexp.size()) return false;
			std::memcpy(f, a_uexp.data() + at, 16);
			return true;
		};
		auto differs = [](const float* f, const std::array<double, 4>& e) {
			for (int i = 0; i < 4; ++i)
				if (std::fabs(static_cast<double>(f[i]) - e[static_cast<size_t>(i)]) > 5e-4) return true;
			return false;
		};
		Bytes out = a_uexp;
		if (kind == "rich_row") {
			const auto [row, colour] = Partition(arg);
			const auto table = a_data.richRows.find("DT_UI_MagicMenu");   // pipeline.rich_row_colour's table
			if (table == a_data.richRows.end()) {
				a_skip = "KeyError: 'DT_UI_MagicMenu'";
				return false;
			}
			const auto entry = table->second.find(std::string(row));
			if (entry == table->second.end()) {
				a_skip = "KeyError: '" + std::string(row) + "'";
				return false;
			}
			float found[4];
			if (!read4(entry->second.offset, found) || differs(found, entry->second.stock)) {
				a_skip = "DT_UI_MagicMenu row " + std::string(row) + ": stock colour at byte " + std::to_string(entry->second.offset) + ", expected " +
				         Repr4(entry->second.stock) + " (game update? re-scan the table)";
				return false;
			}
			const auto it = a_values.find(std::string(colour));
			if (it == a_values.end()) {
				a_skip = "KeyError: '" + std::string(colour) + "'";
				return false;
			}
			const float lin[4] = { static_cast<float>(model::Linear(it->second.v[0])), static_cast<float>(model::Linear(it->second.v[1])),
				static_cast<float>(model::Linear(it->second.v[2])), 1.0f };
			std::memcpy(out.data() + entry->second.offset, lin, 16);
			a_out = std::move(out);
			return true;
		}
		if (kind != "bp_colour" && kind != "material_colour") {
			a_skip = "recipe " + std::string(a_recipe) + " is not ported yet";
			return false;
		}
		const auto bp = a_data.blueprints.find(std::string(arg));
		if (bp == a_data.blueprints.end()) {
			a_skip = "KeyError: '" + std::string(arg) + "'";
			return false;
		}
		for (const auto& c : bp->second) {
			float found[4];
			if (!read4(c.offset, found) || differs(found, c.stock)) {
				a_skip = a_package + ": stock colour " + (read4(c.offset, found) ? Repr4(found) : std::string("()")) + " at byte " +
				         std::to_string(c.offset) + ", expected " + Repr4(c.stock) + " (game update? re-scan the asset)";
				return false;
			}
			model::Value value;
			if (!ConstantColour(c.colour, value)) {
				const auto it = a_values.find(c.colour);
				if (it == a_values.end()) {
					a_skip = "KeyError: '" + c.colour + "'";
					return false;
				}
				value = it->second;
			}
			if (c.hasScale) {
				for (int i = 0; i < 3; ++i) value.v[static_cast<size_t>(i)] = std::min(255.0, value.v[static_cast<size_t>(i)] * c.scale);
				value.n = 3;
			}
			const auto  rgba = LiteralRgba(value);
			const float packed[4] = { static_cast<float>(rgba[0]), static_cast<float>(rgba[1]), static_cast<float>(rgba[2]), found[3] };
			std::memcpy(out.data() + c.offset, packed, 16);
		}
		if (const auto lits = a_data.literals.find(std::string(arg)); lits != a_data.literals.end()) {
			for (const auto& row : lits->second) {
				const size_t width = row.type == "double" ? 8 : 4;
				if (row.offset < 0 || static_cast<std::uint64_t>(row.offset) + width > a_uexp.size()) {
					a_skip = a_package + ": stock value None at byte " + std::to_string(row.offset) + " (" + row.widget + "), expected " + json::PyRepr(row.stock) +
					         " (game update? re-scan the asset)";
					return false;
				}
				double found = 0;
				if (row.type == "double") std::memcpy(&found, a_uexp.data() + row.offset, 8);
				else if (row.type == "int32") {
					std::int32_t v = 0;
					std::memcpy(&v, a_uexp.data() + row.offset, 4);
					found = v;
				} else {
					float v = 0;
					std::memcpy(&v, a_uexp.data() + row.offset, 4);
					found = v;
				}
				if (std::fabs(found - row.stock) > 1e-6) {
					a_skip = a_package + ": stock value " + json::PyRepr(found) + " at byte " + std::to_string(row.offset) + " (" + row.widget + "), expected " +
					         json::PyRepr(row.stock) + " (game update? re-scan the asset)";
					return false;
				}
				if (row.type == "double") {
					const double v = row.value.Number();
					std::memcpy(out.data() + row.offset, &v, 8);
				} else if (row.type == "int32") {
					const auto v = static_cast<std::int32_t>(row.value.type == json::Type::Int ? row.value.i : static_cast<long long>(row.value.f));
					std::memcpy(out.data() + row.offset, &v, 4);
				} else {
					const auto v = static_cast<float>(row.value.Number());
					std::memcpy(out.data() + row.offset, &v, 4);
				}
			}
		}
		a_out = std::move(out);
		return true;
	}
}
