#pragma once
// In-game Pack Builder (Phase 3c): the image recipes of tools/pack_builder/pipeline.py (every kind that decodes the stock
// texture, filters or re-encodes it) ported to C++ on the Pillow port (PackImage.h, PackImageCodec.h): paper, lines and
// laurels, altar, tabs, art and the rest. Python's arithmetic order, round() (half to even), float32 "F" images, sum() and
// CRT pow() are kept, so the results match byte for byte: tests/ingame_pack_recipes_tests.cpp, tests/ingame_pack_parity_tests.py.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "builder/PackCharcoal.h"
#include "builder/PackImage.h"
#include "builder/PackImageCodec.h"
#include "builder/PackRecipes.h"

namespace builder::pack
{
	// what a recipe may read besides its own stock texture (pipeline: stock_dds(name), MENUS, ART); an empty function = not given
	struct BakeInputs
	{
		std::function<bool(const std::string& a_name, Dds& a_out)>    stock;       // another stock texture by name (its every mip, as BakeAsset's own)
		std::function<bool(const std::string& a_file, Bytes& a_out)> menusFile;   // a file of the menus data folder (Stolen-Item.png, character-skill-masks.png)
		std::function<bool(const std::string& a_name, Bytes& a_out)> art;         // the owner's <name>.png (the development-only png recipe)
		std::string                                                   artFolder;   // for the png recipe's skip text
	};

	namespace img
	{
		using recipes::Put16;
		using recipes::Put32;
		using recipes::U16;
		using recipes::U32;

		[[noreturn]] inline void NotBc3() { throw RecipeSkip{ "AssertionError: not BC3" }; }

		inline Bytes FakeHeader(const Dds& a_dds)   // a legacy DDS header for DecodeDds (Image.open reads only these fields)
		{
			Bytes h(128, 0);
			std::memcpy(h.data(), "DDS ", 4);
			Put32(h, 4, 124);
			Put32(h, 8, 0x1007);
			Put32(h, 12, static_cast<std::uint32_t>(a_dds.height));
			Put32(h, 16, static_cast<std::uint32_t>(a_dds.width));
			Put32(h, 76, 32);
			Put32(h, 80, 4);
			std::memcpy(h.data() + 84, a_dds.format == PixelFormat::DXT1 ? "DXT1" : "DXT5", 4);
			return h;
		}

		// decode_rgba / Image.open(dds).convert("RGBA"): the top mip
		inline Image Decode(const Dds& a_dds)
		{
			const size_t pixels = static_cast<size_t>(a_dds.width) * a_dds.height;
			if (a_dds.format == PixelFormat::B8G8R8A8) {
				if (a_dds.body.size() < pixels * 4) throw RecipeSkip{ "ValueError: not enough image data" };
				return FromRaw(Mode::RGBA, a_dds.width, a_dds.height, a_dds.body.data(), "BGRA");
			}
			if (a_dds.format == PixelFormat::G8) {
				if (a_dds.body.size() < pixels) throw RecipeSkip{ "ValueError: not enough image data" };
				return Convert(FromRaw(Mode::L, a_dds.width, a_dds.height, a_dds.body.data(), "L"), Mode::RGBA);
			}
			if (a_dds.format != PixelFormat::DXT1 && a_dds.format != PixelFormat::DXT5) throw RecipeSkip{ "OSError: unsupported DDS format" };
			Bytes file = FakeHeader(a_dds);
			file.insert(file.end(), a_dds.body.begin(), a_dds.body.end());
			auto out = DecodeDds(file);
			if (!out) throw RecipeSkip{ "OSError: truncated DDS" };
			return *out;
		}

		inline double Lum(const std::uint8_t* a_p) { return 0.2126 * a_p[0] + 0.7152 * a_p[1] + 0.0722 * a_p[2]; }   // pipeline.lum
		inline double Lum601(const std::uint8_t* a_p) { return 0.299 * a_p[0] + 0.587 * a_p[1] + 0.114 * a_p[2]; }
		inline int    Spread(const std::uint8_t* a_p) { return std::max({ a_p[0], a_p[1], a_p[2] }) - std::min({ a_p[0], a_p[1], a_p[2] }); }
		inline double Clamp01(double a_v) { return std::min(1.0, std::max(0.0, a_v)); }
		inline size_t Count(const Image& a_img) { return static_cast<size_t>(a_img.width) * a_img.height; }

		inline Image GreyOf(const Image& a_rgba)   // grey_of
		{
			Image out = NewImage(Mode::L, a_rgba.width, a_rgba.height);
			for (size_t i = 0; i < out.px.size(); ++i) out.px[i] = py::Clip8(py::Round(Lum(&a_rgba.px[i * 4])));
			return out;
		}

		template <class T> T Pct(const std::vector<T>& a_sorted, int a_pct) { return a_sorted[a_sorted.size() * static_cast<size_t>(a_pct) / 100]; }

		inline std::vector<std::pair<int, int>> MipSizes(int a_w, int a_h, int a_count)   // mip_sizes
		{
			std::vector<std::pair<int, int>> out;
			for (int i = 0; i < std::max(1, a_count); ++i) out.emplace_back(std::max(1, a_w >> i), std::max(1, a_h >> i));
			return out;
		}

		inline bool IsBgra(const Dds& a_dds) { return a_dds.format == PixelFormat::B8G8R8A8; }

		// colour_keep_alpha: every mip of art BC3-encoded, only the colour halves taken (stock alpha byte for byte)
		inline Bytes ColourKeepAlpha(const Dds& a_stock, const Image& a_art)
		{
			if (a_stock.format != PixelFormat::DXT5) NotBc3();
			Bytes  blocks = a_stock.body;
			size_t at = 0;
			for (auto [w, h] : MipSizes(a_stock.width, a_stock.height, a_stock.mips)) {
				const Bytes encoded = EncodeBc(Resize(a_art, w, h, Resample::Lanczos), BcFormat::DXT5);
				for (size_t offset = 0; offset < encoded.size(); offset += 16) {
					if (at + offset + 16 > blocks.size()) throw RecipeSkip{ "AssertionError: size mismatch" };
					std::memcpy(blocks.data() + at + offset + 8, encoded.data() + offset + 8, 8);
				}
				at += encoded.size();
			}
			if (at != blocks.size()) throw RecipeSkip{ "AssertionError: size mismatch" };
			return blocks;
		}

		// colour_bc1: every mip of art BC1-encoded, whole blocks replaced
		inline Bytes ColourBc1(const Dds& a_stock, const Image& a_art)
		{
			if (a_stock.format != PixelFormat::DXT1) throw RecipeSkip{ "TypeError: slice indices must be integers or None or have an __index__ method" };
			Bytes  blocks = a_stock.body;
			size_t at = 0;
			for (auto [w, h] : MipSizes(a_stock.width, a_stock.height, a_stock.mips)) {
				const Bytes encoded = EncodeBc(Resize(a_art, w, h, Resample::Lanczos), BcFormat::DXT1);
				if (at + encoded.size() > blocks.size()) throw RecipeSkip{ "AssertionError: size mismatch" };
				std::memcpy(blocks.data() + at, encoded.data(), encoded.size());
				at += encoded.size();
			}
			if (at != blocks.size()) throw RecipeSkip{ "AssertionError: size mismatch" };
			return blocks;
		}

		// full_rgba: colour and alpha (raw BGRA, or every mip BC3-encoded)
		inline Bytes FullRgba(const Dds& a_stock, const Image& a_image)
		{
			const Image rgba = Convert(a_image, Mode::RGBA);
			if (IsBgra(a_stock)) return ToRaw(rgba, "BGRA");
			if (a_stock.format != PixelFormat::DXT5) NotBc3();
			Bytes  blocks = a_stock.body;
			size_t at = 0;
			for (auto [w, h] : MipSizes(a_stock.width, a_stock.height, std::max(1, a_stock.mips))) {
				const Bytes encoded = EncodeBc(Resize(rgba, w, h, Resample::Lanczos), BcFormat::DXT5);
				if (at + encoded.size() > blocks.size()) throw RecipeSkip{ "AssertionError: size mismatch" };
				std::memcpy(blocks.data() + at, encoded.data(), encoded.size());
				at += encoded.size();
			}
			if (at != blocks.size()) throw RecipeSkip{ "AssertionError: size mismatch" };
			return blocks;
		}

		// splice_colour: the top mip BC3-encoded, its colour halves into the stock blocks (a_encoded: the colour.dds blocks)
		inline Bytes SpliceColour(const Dds& a_stock, const Image& a_rgba, Bytes* a_encoded = nullptr)
		{
			const Bytes replacement = EncodeBc(Convert(a_rgba, Mode::RGBA), BcFormat::DXT5);
			if (a_stock.format != PixelFormat::DXT5) NotBc3();
			Bytes blocks = a_stock.body;
			if (blocks.size() != replacement.size()) throw RecipeSkip{ "AssertionError: size mismatch" };
			for (size_t offset = 0; offset < blocks.size(); offset += 16) std::memcpy(blocks.data() + offset + 8, replacement.data() + offset + 8, 8);
			if (a_encoded) *a_encoded = replacement;
			return blocks;
		}

		template <class F> Bytes PerPixel(const Dds& a_stock, F a_fn)   // per_pixel: fn(p) -> RGB, alpha 255, spliced
		{
			const Image image = Decode(a_stock);
			Image       out = NewImage(Mode::RGBA, image.width, image.height);
			for (size_t i = 0; i < Count(image); ++i) {
				const auto c = a_fn(&image.px[i * 4]);
				for (int k = 0; k < 3; ++k) out.px[i * 4 + static_cast<size_t>(k)] = py::Clip8(c[static_cast<size_t>(k)]);
				out.px[i * 4 + 3] = 255;
			}
			return SpliceColour(a_stock, out);
		}

		inline const std::array<int, 32>& Grey5() { return charcoal::Grey5(); }
		inline constexpr int kBayer4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

		inline void PutGreyBlock(Bytes& a_out, size_t a_at, int a_k0, int a_k1, std::uint32_t a_index)
		{
			Put16(a_out, a_at, charcoal::GreyCode(a_k1));
			Put16(a_out, a_at + 2, charcoal::GreyCode(a_k0));
			Put32(a_out, a_at + 4, a_index);
		}

		// neutral_grey_colour over the colour halves of the top mip's BC3 blocks (values: width x height, row order)
		inline Bytes NeutralGreyColour(const Bytes& a_blocks, const std::vector<double>& a_values, int a_w, int a_h, bool a_dither)
		{
			Bytes  out = a_blocks;
			size_t at = 0;
			for (int by = 0; by < a_h; by += 4)
				for (int bx = 0; bx < a_w; bx += 4) {
					double block[16];
					for (int y = 0; y < 4; ++y)
						for (int x = 0; x < 4; ++x) block[y * 4 + x] = a_values[static_cast<size_t>(std::min(by + y, a_h - 1)) * a_w + std::min(bx + x, a_w - 1)];
					int k0, k1;
					charcoal::Bracket(*std::min_element(block, block + 16), *std::max_element(block, block + 16), k0, k1);
					const double p0 = Grey5()[static_cast<size_t>(k1)], p1 = Grey5()[static_cast<size_t>(k0)];
					const double palette[4] = { p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3 };
					const double third = (p0 - p1) / 3;
					std::uint32_t index = 0;
					for (int i = 0; i < 16; ++i) {
						double v = block[i];
						if (a_dither) v += (kBayer4[i] / 16.0 - 0.5) * third;
						index |= static_cast<std::uint32_t>(charcoal::Nearest4(palette, v)) << (2 * i);
					}
					if (at + 16 > out.size()) throw RecipeSkip{ "struct.error: pack_into requires a buffer of at least " + std::to_string(at + 16) + " bytes" };
					PutGreyBlock(out, at + 8, k0, k1, index);
					at += 16;
				}
			return out;
		}

		// neutral_grey_fs: the same with Floyd-Steinberg error diffusion across the block borders
		inline Bytes NeutralGreyFs(const Bytes& a_blocks, const std::vector<double>& a_values, int a_w, int a_h)
		{
			Bytes                     out = a_blocks;
			std::vector<double>       rows = a_values;
			const int                 bw = (a_w + 3) / 4, bh = (a_h + 3) / 4;
			std::vector<std::array<int, 2>>    ends;
			std::vector<std::array<double, 4>> palettes;
			for (int by = 0; by < bh; ++by)
				for (int bx = 0; bx < bw; ++bx) {
					double lo = 0, hi = 0;
					for (int i = 0; i < 16; ++i) {
						const double v = rows[static_cast<size_t>(std::min(by * 4 + i / 4, a_h - 1)) * a_w + std::min(bx * 4 + i % 4, a_w - 1)];
						lo = i ? std::min(lo, v) : v;
						hi = i ? std::max(hi, v) : v;
					}
					int k0, k1;
					charcoal::Bracket(lo, hi, k0, k1);
					const double p0 = Grey5()[static_cast<size_t>(k1)], p1 = Grey5()[static_cast<size_t>(k0)];
					ends.push_back({ k0, k1 });
					palettes.push_back({ p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3 });
				}
			std::vector<std::uint8_t> index(rows.size());
			for (int y = 0; y < a_h; ++y) {
				double*   row = &rows[static_cast<size_t>(y) * a_w];
				double*   below = y + 1 < a_h ? &rows[static_cast<size_t>(y + 1) * a_w] : nullptr;
				const int base = (y / 4) * bw;
				for (int x = 0; x < a_w; ++x) {
					const auto&  pal = palettes[static_cast<size_t>(base + x / 4)];
					const double palette[4] = { pal[0], pal[1], pal[2], pal[3] };
					const double v = row[x];
					const int    j = charcoal::Nearest4(palette, v);
					index[static_cast<size_t>(y) * a_w + x] = static_cast<std::uint8_t>(j);
					const double err = v - palette[j];
					if (x + 1 < a_w) row[x + 1] += err * 7 / 16;
					if (below) {
						if (x > 0) below[x - 1] += err * 3 / 16;
						below[x] += err * 5 / 16;
						if (x + 1 < a_w) below[x + 1] += err / 16;
					}
				}
			}
			size_t at = 0;
			for (int by = 0; by < bh; ++by)
				for (int bx = 0; bx < bw; ++bx) {
					const auto&   e = ends[static_cast<size_t>(by * bw + bx)];
					std::uint32_t bits = 0;
					for (int i = 0; i < 16; ++i)
						bits |= static_cast<std::uint32_t>(index[static_cast<size_t>(std::min(by * 4 + i / 4, a_h - 1)) * a_w + std::min(bx * 4 + i % 4, a_w - 1)]) << (2 * i);
					if (at + 16 > out.size()) throw RecipeSkip{ "struct.error: pack_into requires a buffer of at least " + std::to_string(at + 16) + " bytes" };
					PutGreyBlock(out, at + 8, e[0], e[1], bits);
					at += 16;
				}
			return out;
		}

		// bc3_alpha_block
		inline void Bc3AlphaBlock(const int (&a_values)[16], std::uint8_t* a_dst)
		{
			const int hi = *std::max_element(a_values, a_values + 16), lo = *std::min_element(a_values, a_values + 16);
			if (hi > 255 || lo < 0) throw RecipeSkip{ "ValueError: bytes must be in range(0, 256)" };
			a_dst[0] = static_cast<std::uint8_t>(hi);
			a_dst[1] = static_cast<std::uint8_t>(lo);
			std::uint64_t bits = 0;
			if (hi != lo) {
				double palette[8] = { static_cast<double>(hi), static_cast<double>(lo) };
				for (int k = 1; k < 7; ++k) palette[k + 1] = ((7 - k) * hi + k * lo) / 7.0;
				for (int i = 0; i < 16; ++i) {
					int best = 0;
					for (int c = 1; c < 8; ++c)
						if (std::fabs(palette[c] - a_values[i]) < std::fabs(palette[best] - a_values[i])) best = c;
					bits |= std::uint64_t(best) << (3 * i);
				}
			}
			for (int i = 0; i < 6; ++i) a_dst[2 + i] = static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFF);
		}

		inline double ParchmentCurve(double a_v, double a_low, double a_high, double a_dark, double a_light)
		{
			const double t = a_high > a_low ? std::min(1.0, std::max(0.0, (a_v - a_low) / (a_high - a_low))) : 0.5;
			return a_dark + (a_light - a_dark) * t;
		}

		inline double PaperTone(double a_dark, double a_light, double a_panel) { return ((a_dark + a_light) / 2 + a_panel) / 2; }

		inline std::vector<double> RoundedLums(const Image& a_rgba, int a_alphaAbove)   // sorted round(lum(p)) of pixels with alpha > a
		{
			std::vector<double> v;
			for (size_t i = 0; i < Count(a_rgba); ++i)
				if (a_rgba.px[i * 4 + 3] > a_alphaAbove) v.push_back(py::Round(Lum(&a_rgba.px[i * 4])));
			std::sort(v.begin(), v.end());
			return v;
		}
		inline std::vector<double> Lums(const Image& a_rgba, int a_alphaAbove)   // sorted lum(p) of pixels with alpha > a
		{
			std::vector<double> v;
			for (size_t i = 0; i < Count(a_rgba); ++i)
				if (a_rgba.px[i * 4 + 3] > a_alphaAbove) v.push_back(Lum(&a_rgba.px[i * 4]));
			std::sort(v.begin(), v.end());
			return v;
		}

		// ---- paper ----

		inline Bytes Parchment(const Dds& a_stock, double a_dark, double a_light)
		{
			auto values = RoundedLums(Decode(a_stock), 128);
			if (values.empty()) values = { 0, 255 };
			const double low = Pct(values, 2), high = Pct(values, 98);
			Bytes out = recipes::GreyBlocks(a_stock.format, a_stock.body, [&](double v) { return ParchmentCurve(v, low, high, a_dark, a_light); });
			if (out.empty()) NotBc3();
			return out;
		}

		inline Bytes PaperScale(const Dds& a_stock, double a_light)
		{
			std::vector<double> values;
			const bool          r8 = a_stock.format == PixelFormat::G8;
			if (r8) {
				for (auto b : a_stock.body) values.push_back(b);
				std::sort(values.begin(), values.end());
			} else
				values = RoundedLums(Decode(a_stock), 128);
			double high = values.empty() ? 255.0 : Pct(values, 98);
			if (high == 0) high = 255;
			auto scale = [&](double v) { return std::max(0.0, std::min(255.0, py::Round(v * a_light / high))); };
			if (r8) {
				Bytes out = a_stock.body;
				for (auto& b : out) b = static_cast<std::uint8_t>(scale(b));
				return out;
			}
			Bytes out = recipes::GreyBlocks(a_stock.format, a_stock.body, scale);
			if (out.empty()) NotBc3();
			return out;
		}

		inline Bytes FsInto(const Dds& a_stock, const std::vector<double>& a_values, int a_w, int a_h)
		{
			if (a_stock.format != PixelFormat::DXT5) NotBc3();
			return NeutralGreyFs(a_stock.body, a_values, a_w, a_h);
		}

		inline Bytes SmoothGrain(const Dds& a_stock, double a_dark, double a_light, double a_blur, double a_amount, const double* a_mid)
		{
			if (a_stock.format != PixelFormat::DXT5) NotBc3();
			const Image image = Decode(a_stock);
			const Image grey = GreyOf(image);
			std::vector<double> values;
			for (size_t i = 0; i < Count(image); ++i)
				if (image.px[i * 4 + 3] > 128) values.push_back(grey.px[i]);
			std::sort(values.begin(), values.end());
			if (values.empty()) values = { 0, 255 };
			const double low = Pct(values, 2), high = Pct(values, 98);
			const Image  soft = a_blur != 0 ? GaussianBlur(grey, static_cast<float>(a_blur)) : grey;
			const double mid = a_mid ? *a_mid : (a_dark + a_light) / 2;
			const double half = (a_light - a_dark) / 2 * a_amount;
			double       span = high - low;
			if (span == 0) span = 1;
			double lut[256];
			for (int v = 0; v < 256; ++v) lut[v] = std::max(0.0, std::min(255.0, mid - half + 2 * half * std::min(1.0, std::max(0.0, (v - low) / span))));
			std::vector<double> target(soft.px.size());
			for (size_t i = 0; i < target.size(); ++i) target[i] = lut[soft.px[i]];
			return NeutralGreyFs(a_stock.body, py::F32(target), image.width, image.height);
		}

		inline Bytes PageGrain(const Dds& a_stock, double a_frame, double a_panel, double a_blur, double a_amount, const double* a_tone)
		{
			if (a_stock.format != PixelFormat::DXT5) NotBc3();
			const Image image = Decode(a_stock);
			const Image grey = GreyOf(image);
			const Image soft = GaussianBlur(grey, static_cast<float>(a_blur));
			std::vector<double> paper;
			for (size_t i = 0; i < Count(image); ++i)
				if (grey.px[i] > 116 + 20 && image.px[i * 4 + 3] > 128) paper.push_back(grey.px[i]);
			std::sort(paper.begin(), paper.end());
			if (paper.empty()) paper = { 0, 255 };
			const double low = Pct(paper, 2), high = Pct(paper, 98);
			double       span = high - low;
			if (span == 0) span = 1;
			const double panel = a_tone ? *a_tone : a_panel;
			std::vector<double> target(grey.px.size());
			for (size_t i = 0; i < target.size(); ++i) {
				const double v = grey.px[i], s = soft.px[i];
				const double t = std::min(1.0, std::max(0.0, (v - 42) / (116 - 42)));
				const double w = std::min(1.0, std::max(0.0, (v - 116) / 20));
				const double grain = (std::min(1.0, std::max(0.0, (s - low) / span)) - 0.5) * 2 * a_amount;
				target[i] = std::max(0.0, std::min(255.0, a_frame + (panel - a_frame) * t + w * grain));
			}
			return NeutralGreyFs(a_stock.body, py::F32(target), image.width, image.height);
		}

		enum class InvertMode { Clamp, Frame, Shadow };

		// invert_paper as float targets (the F image's values) or the encoded body
		inline std::vector<double> InvertTargets(const Image& a_image, double a_tone, double a_blur, InvertMode a_mode, double a_frame, double a_panel)
		{
			const size_t        n = Count(a_image);
			std::vector<double> values(n);
			for (size_t i = 0; i < n; ++i) values[i] = Lum(&a_image.px[i * 4]);
			const double        edge = a_mode == InvertMode::Frame ? 116 + 20 : 0;
			std::vector<double> paper;
			for (size_t i = 0; i < n; ++i)
				if (a_image.px[i * 4 + 3] > 128 && values[i] > edge) paper.push_back(values[i]);
			std::sort(paper.begin(), paper.end());
			if (paper.empty()) paper = { 128.0 };
			if (a_mode == InvertMode::Shadow) paper.erase(paper.begin(), paper.begin() + static_cast<std::ptrdiff_t>(paper.size() / 5));
			if (paper.empty()) throw RecipeSkip{ "IndexError: list index out of range" };
			const double low = Pct(paper, 2), high = Pct(paper, 98), middle = paper[paper.size() / 2];
			double       invMiddle = 255 - middle;
			if (invMiddle == 0) invMiddle = 1;
			auto mapped = [&](double v) { return a_tone * (255 - std::min(high, std::max(low, v))) / invMiddle; };
			std::vector<double> soft = values;
			if (a_blur != 0) {
				Image source = NewImage(Mode::L, a_image.width, a_image.height);
				for (size_t i = 0; i < n; ++i)
					source.px[i] = py::Clip8(py::Round(a_image.px[i * 4 + 3] > 128 && values[i] > edge ? std::min(high, std::max(low, values[i])) : middle));
				soft = py::Values(GaussianBlur(source, static_cast<float>(a_blur)));
			}
			std::vector<double> target(n);
			for (size_t i = 0; i < n; ++i) {
				const double v = values[i];
				double       out = mapped(soft[i]);
				if (a_mode == InvertMode::Frame) {
					const double t = std::min(1.0, std::max(0.0, (v - 42) / (116 - 42)));
					const double f = a_frame + (a_panel - a_frame) * t;
					const double w = std::min(1.0, std::max(0.0, (v - 116) / 20));
					out = f + (out - f) * w;
				} else if (a_mode == InvertMode::Shadow && v < low)
					out = mapped(low) * v / low;
				target[i] = std::max(0.0, std::min(255.0, out));
			}
			return py::F32(target);
		}

		inline Bytes InvertPaper(const Dds& a_stock, double a_tone, double a_blur, InvertMode a_mode = InvertMode::Clamp, double a_frame = 0, double a_panel = 0)
		{
			if (a_stock.format != PixelFormat::DXT5) NotBc3();
			const Image image = Decode(a_stock);
			return NeutralGreyFs(a_stock.body, InvertTargets(image, a_tone, a_blur, a_mode, a_frame, a_panel), image.width, image.height);
		}

		inline std::pair<int, int> DetailsBand(const Dds& a_stock, double a_tint)   // details_tone/details_smooth's (dark, light)
		{
			auto values = RoundedLums(Decode(a_stock), 128);
			if (values.empty()) values = { 0, 255 };
			return { py::Srgb(py::Linear(Pct(values, 2)) * a_tint), py::Srgb(py::Linear(Pct(values, 98)) * a_tint) };
		}

		inline Bytes GraySmooth(const Dds& a_stock, double a_blur)
		{
			if (a_stock.format != PixelFormat::DXT5) NotBc3();
			Image grey = GreyOf(Decode(a_stock));
			if (a_blur != 0) grey = GaussianBlur(grey, static_cast<float>(a_blur));
			return NeutralGreyColour(a_stock.body, py::Values(grey), grey.width, grey.height, true);
		}

		inline Bytes PopupPanel(const Dds& a_stock, const model::Value& a_paper, double a_side, double a_top)
		{
			const int   width = a_stock.width, height = a_stock.height, mips = std::max(1, a_stock.mips);
			const Image alpha = GetChannel(Decode(a_stock), 3);
			auto        ramp = [](int n, double frac) {
                std::vector<int> out;
                for (int i = 0; i < n; ++i) out.push_back(py::RoundI(255 * std::min(1.0, std::min(i, n - 1 - i) / (n * frac))));
                return out;
			};
			const Image across = py::LFrom(width, 1, ramp(width, 0.12)), down = py::LFrom(1, height, ramp(height, 0.22));
			const Image depth = Darker(Resize(across, width, height, Resample::Nearest), Resize(down, width, height, Resample::Nearest));
			double      lut[256];
			for (int d = 0; d < 256; ++d) lut[d] = 255 * (1 - py::Pow(1 - d / 255.0, 2));
			std::vector<Image> bands;
			const int ix = py::RoundI(width * 34 / 2048.0), iy = py::RoundI(height * 52 / 1024.0);
			const int lw = std::max(1, py::RoundI(width * 3 / 2048.0)), lh = std::max(1, py::RoundI(height * 4 / 1024.0));
			for (int k = 0; k < 3; ++k) {
				const double c = a_paper.v[static_cast<size_t>(k)];
				const double edge = c * 16 / 33;
				std::array<std::uint8_t, 256> table{};
				for (int d = 0; d < 256; ++d) table[static_cast<size_t>(d)] = py::Clip8(py::Round(edge + (c - edge) * lut[d] / 255));
				Image        band = Point(depth, table);
				const auto   line = py::Clip8(py::Round(c * 64 / 33));
				const Colour fill{ line };
				DrawRectangle(band, ix, iy, width - 1 - ix, iy + lh - 1, fill);
				DrawRectangle(band, ix, height - iy - lh, width - 1 - ix, height - 1 - iy, fill);
				DrawRectangle(band, ix, iy, ix + lw - 1, height - 1 - iy, fill);
				DrawRectangle(band, width - ix - lw, iy, width - 1 - ix, height - 1 - iy, fill);
				bands.push_back(std::move(band));
			}
			bands.push_back(alpha);
			const Image panel = Merge(Mode::RGBA, bands);
			const int   w = py::RoundI(width * (1 - 2 * a_side)), h = py::RoundI(height * (1 - 2 * a_top));
			Image       shape = NewImage(Mode::RGBA, width, height, { 0, 0, 0, 0 });
			AlphaCompositeAt(shape, Resize(panel, w, h, Resample::Lanczos), py::FloorDiv(width - w, 2), py::FloorDiv(height - h, 2));
			const double mx = std::max({ a_paper.v[0], a_paper.v[1], a_paper.v[2] }), mn = std::min({ a_paper.v[0], a_paper.v[1], a_paper.v[2] });
			const bool   neutral = mx - mn <= 3;
			Bytes        blocks;
			for (auto [mw, mh] : MipSizes(width, height, mips)) {
				const Image mip = Resize(shape, mw, mh, Resample::Lanczos);
				Bytes       encoded = EncodeBc(mip, BcFormat::DXT5);
				if (neutral) encoded = NeutralGreyColour(encoded, py::Values(Convert(mip, Mode::L)), mw, mh, true);
				blocks.insert(blocks.end(), encoded.begin(), encoded.end());
			}
			if (a_stock.format != PixelFormat::DXT5) NotBc3();
			if (blocks.size() != a_stock.body.size()) throw RecipeSkip{ "popup_panel: encoded size differs from the stock texture" };
			return blocks;
		}

		inline Bytes PopupPaper(const Dds& a_stock, double a_dark, double a_light, const std::array<double, 3>& a_laurel, double a_strength)
		{
			const Image image = Decode(a_stock);
			const int   width = image.width, height = image.height;
			const Image grey = GreyOf(image);
			const Image local = BoxBlur(grey, 8);
			const Image behind = BoxBlur(MinFilter(grey, 9), 4);
			auto        values = Lums(image, 128);
			if (values.empty()) values = { 0, 255 };
			const double low = Pct(values, 2), mid = values[values.size() / 2];
			const double top = 0.075 * height, bottom = 0.915 * height;
			const double ramp = std::max(1.0, std::min(16.0, top / 3));
			Image        result = NewImage(Mode::RGBA, width, height);
			for (size_t i = 0; i < Count(image); ++i) {
				const std::uint8_t* p = &image.px[i * 4];
				const double        v = Lum(p);
				const int           y = static_cast<int>(i / static_cast<size_t>(width));
				const bool          inBand = p[3] >= 250 && (y < top || y > bottom);
				const double        depth = inBand ? std::min(1.0, std::max({ top - y, y - bottom, 0.0 }) / ramp) : 0.0;
				const double        base = ParchmentCurve(std::min(v, mid), low, mid, a_dark, a_light) * (1 - depth) +
				                    ParchmentCurve(std::min(static_cast<double>(behind.px[i]), mid), low, mid, a_dark, a_light) * depth;
				double t = 0.0;
				if (inBand) t = std::min(1.0, std::max(0.0, (v - local.px[i] - 3) / 14)) * a_strength;
				for (int k = 0; k < 3; ++k) result.px[i * 4 + static_cast<size_t>(k)] = py::Clip8(py::Round(base * (1 - t) + a_laurel[static_cast<size_t>(k)] * t));
				result.px[i * 4 + 3] = p[3];
			}
			return ColourKeepAlpha(a_stock, result);
		}

		// ---- lines and laurels ----

		template <class F> Bytes Mapped(const Dds& a_stock, F a_fn)
		{
			Bytes out = recipes::MapColours(a_stock.format, a_stock.body, a_fn);
			if (out.empty() && !a_stock.body.empty()) NotBc3();
			return out;
		}

		inline Bytes ScaledByLum(const Dds& a_stock, const std::array<double, 3>& a_target, double a_reference)
		{
			return Mapped(a_stock, [&](const recipes::Colour& c) {
				const double l = recipes::Lum(c);
				return recipes::Colour{ std::min(255.0, a_target[0] * l / a_reference), std::min(255.0, a_target[1] * l / a_reference),
					std::min(255.0, a_target[2] * l / a_reference) };
			});
		}

		inline Bytes BandLines(const Dds& a_stock, const std::array<double, 3>& a_rgb, double a_scale)
		{
			auto values = Lums(Decode(a_stock), 128);
			if (values.empty()) values = { 255 };
			double high = values[values.size() - std::max<size_t>(1, values.size() / 1000)];
			if (high == 0) high = 255;
			return ScaledByLum(a_stock, { std::min(255.0, a_rgb[0] * a_scale), std::min(255.0, a_rgb[1] * a_scale), std::min(255.0, a_rgb[2] * a_scale) }, high);
		}

		inline Bytes LineTint(const Dds& a_stock, const std::array<double, 3>& a_rgb, double a_scale)
		{
			auto values = Lums(Decode(a_stock), 0);
			if (values.empty()) values = { 255 };
			double high = Pct(values, 98);
			if (high == 0) high = 255;
			return ScaledByLum(a_stock, { std::min(255.0, a_rgb[0] * a_scale), std::min(255.0, a_rgb[1] * a_scale), std::min(255.0, a_rgb[2] * a_scale) }, high);
		}

		inline Bytes LaurelTint(const Dds& a_stock, const std::array<double, 3>& a_rgb, const double* a_reference)
		{
			double reference;
			if (a_reference) reference = *a_reference;
			else {
				auto values = Lums(Decode(a_stock), 128);
				if (values.empty()) values = { 255 };
				reference = Pct(values, 98);
				if (reference == 0) reference = 255;
			}
			if (reference == 0) throw RecipeSkip{ "ZeroDivisionError: float division by zero" };
			return ScaledByLum(a_stock, a_rgb, reference);
		}

		inline Bytes FlatAlpha(const Dds& a_stock, const std::array<double, 3>& a_rgb, double a_scale)
		{
			Bytes out = recipes::FlatBlocks(a_stock.format, a_stock.body, a_rgb);
			const Image alpha = GetChannel(Decode(a_stock), 3);
			if (a_stock.format != PixelFormat::DXT5) NotBc3();
			const int width = alpha.width, height = alpha.height;
			size_t    at = 0;
			for (int by = 0; by < height; by += 4)
				for (int bx = 0; bx < width; bx += 4) {
					int values[16];
					for (int y = 0; y < 4; ++y)
						for (int x = 0; x < 4; ++x)
							values[y * 4 + x] = py::RoundI(alpha.px[static_cast<size_t>(std::min(by + y, height - 1)) * width + std::min(bx + x, width - 1)] * a_scale);
					std::uint8_t block[8];
					Bc3AlphaBlock(values, block);
					std::memcpy(out.data() + at, block, 8);
					at += 16;
				}
			return out;
		}

		// ---- altar ----

		inline std::array<double, 3> AltarMetal(const std::uint8_t* a_p, const std::array<double, 3>& a_metal)
		{
			const double l = Lum(a_p);
			const double k = l > 0 ? py::Pow(l / 150, 0.8) : 0.0;
			return { a_metal[0] * k, a_metal[1] * k, a_metal[2] * k };
		}

		inline Bytes WriteBgra(const Dds& a_stock, const Image& a_result)   // the BGRA stock with the result's colour (alpha kept)
		{
			Bytes raw = a_stock.body;
			for (size_t i = 0; i < Count(a_result); ++i) {
				raw[4 * i + 2] = a_result.px[4 * i];
				raw[4 * i + 1] = a_result.px[4 * i + 1];
				raw[4 * i + 0] = a_result.px[4 * i + 2];
			}
			return raw;
		}

		inline Bytes NeutralPaperBlocks(const Bytes& a_body, const Image& a_rgba)   // neutral_paper_blocks (tolerance 8)
		{
			const int    width = a_rgba.width, height = a_rgba.height;
			const int    bw = (width + 3) / 4, bh = (height + 3) / 4;
			const size_t top = static_cast<size_t>(bw) * bh * 16;
			const Bytes  topBlocks(a_body.begin(), a_body.begin() + static_cast<std::ptrdiff_t>(std::min(top, a_body.size())));
			const Bytes  fs = NeutralGreyFs(topBlocks, py::Values(GreyOf(a_rgba)), width, height);
			Bytes        out = a_body;
			for (int by = 0; by < bh; ++by)
				for (int bx = 0; bx < bw; ++bx) {
					bool skip = false;
					for (int y = 0; y < 4 && !skip; ++y)
						for (int x = 0; x < 4 && !skip; ++x)
							if (Spread(a_rgba.At(std::min(bx * 4 + x, width - 1), std::min(by * 4 + y, height - 1))) > 8) skip = true;
					if (skip) continue;
					const size_t at = static_cast<size_t>(by * bw + bx) * 16;
					std::memcpy(out.data() + at + 8, fs.data() + at + 8, 8);
				}
			return out;
		}

		struct PanelInvert
		{
			double tone = 0, blur = 0;
			bool   hasClean = false;   // the four-value form (tone, blur, clean, stain)
			double clean = 0, stain = 0;
		};

		inline Bytes AltarPanel(const Dds& a_stock, const std::array<double, 3>& a_metal, const std::array<double, 3>* a_paper, const Dds* a_mask, int a_channel,
			const PanelInvert* a_invert)
		{
			const Image  image = Decode(a_stock);
			const size_t n = Count(image);
			const int    width = image.width, height = image.height;
			std::vector<double> lums(n);
			for (size_t i = 0; i < n; ++i) lums[i] = Lum(&image.px[i * 4]);
			const double stain = a_invert && a_invert->hasClean ? a_invert->stain : 0.0;
			Image        nearMetal = NewImage(Mode::L, width, height);
			if (stain != 0) {
				Image dark = NewImage(Mode::L, width, height);
				for (size_t i = 0; i < n; ++i) dark.px[i] = lums[i] < 120 ? 255 : 0;
				nearMetal = MaxFilter(dark, 21);
			}
			Image weight = NewImage(Mode::L, width, height);
			for (size_t i = 0; i < n; ++i) {
				const std::uint8_t* p = &image.px[i * 4];
				const double        allow = 45 + (nearMetal.px[i] ? 0.0 : stain);
				weight.px[i] = py::Clip8(py::Round(255 * Clamp01((lums[i] - 140) / 25) * Clamp01((allow - Spread(p)) / 20)));
			}
			weight = GaussianBlur(MedianFilter(weight, 9), 1.2f);
			if (a_mask) weight = GaussianBlur(Resize(GetChannel(Decode(*a_mask), a_channel), width, height, Resample::Bilinear), 1.0f);
			Image gem = NewImage(Mode::L, width, height);
			for (size_t i = 0; i < n; ++i) {
				const std::uint8_t* p = &image.px[i * 4];
				gem.px[i] = py::Clip8(py::Round(255 * Clamp01((p[2] - p[0] - 20) / 25.0) * Clamp01((Spread(p) - 30) / 30.0)));
			}
			gem = MedianFilter(gem, 3);
			std::vector<double> paperL;
			for (size_t i = 0; i < n; ++i)
				if (weight.px[i] > 200) paperL.push_back(lums[i]);
			const double mean = paperL.empty() ? 200.0 : py::Sum(paperL) / static_cast<double>(paperL.size());
			std::vector<double> inverted;
			if (a_invert) {
				auto ordered = paperL;
				std::sort(ordered.begin(), ordered.end());
				if (ordered.empty()) ordered = { 200.0 };
				const double low = Pct(ordered, 2), high = Pct(ordered, 98), middle = ordered[ordered.size() / 2];
				Image        source = NewImage(Mode::L, width, height);
				for (size_t i = 0; i < n; ++i) source.px[i] = py::Clip8(py::Round(weight.px[i] > 128 ? std::min(high, std::max(low, lums[i])) : middle));
				const Image soft = a_invert->blur != 0 ? GaussianBlur(source, static_cast<float>(a_invert->blur)) : source;
				double      inv = 255 - middle;
				if (inv == 0) inv = 1;
				inverted.resize(n);
				for (size_t i = 0; i < n; ++i) inverted[i] = a_invert->tone * (255 - soft.px[i]) / inv;
				if (a_invert->hasClean && a_invert->clean != 0) {
					Image plane = NewImage(Mode::L, width, height);
					for (size_t i = 0; i < n; ++i) plane.px[i] = py::Clip8(py::Round(inverted[i]));
					const Image lowPass = GaussianBlur(plane, static_cast<float>(a_invert->clean));
					for (size_t i = 0; i < n; ++i) inverted[i] = a_invert->tone + (inverted[i] - lowPass.px[i]);
				}
			}
			Image result = NewImage(Mode::RGBA, width, height);
			for (size_t i = 0; i < n; ++i) {
				const std::uint8_t* p = &image.px[i * 4];
				const double        w = weight.px[i] / 255.0, g = gem.px[i] / 255.0, v = lums[i];
				const double        grain = v > 0 ? std::min(1.2, std::max(0.8, py::Pow(v / mean, 0.6))) : 0.8;
				const auto          m = AltarMetal(p, a_metal);
				for (int k = 0; k < 3; ++k) {
					const double tint = a_invert ? inverted[i] : (*a_paper)[static_cast<size_t>(k)] * grain;
					double       c = tint * w + m[static_cast<size_t>(k)] * (1 - w);
					c = c * (1 - g) + p[k] * g;
					result.px[i * 4 + static_cast<size_t>(k)] = py::Clip8(py::Round(c));
				}
				result.px[i * 4 + 3] = p[3];
			}
			if (IsBgra(a_stock)) return WriteBgra(a_stock, result);
			return NeutralPaperBlocks(ColourKeepAlpha(a_stock, result), result);
		}

		inline Bytes FrameMetal(const Dds& a_stock, const std::array<double, 3>& a_metal)
		{
			const Image  image = Decode(a_stock);
			const size_t n = Count(image);
			Image        gem = NewImage(Mode::L, image.width, image.height);
			for (size_t i = 0; i < n; ++i) {
				const std::uint8_t* p = &image.px[i * 4];
				gem.px[i] = py::Clip8(py::Round(255 * Clamp01((std::max(p[1], p[2]) - p[0] - 20) / 25.0) * Clamp01((Spread(p) - 30) / 30.0)));
			}
			if (std::min(image.width, image.height) >= 3) gem = MedianFilter(gem, 3);
			Image result = NewImage(Mode::RGBA, image.width, image.height);
			for (size_t i = 0; i < n; ++i) {
				const std::uint8_t* p = &image.px[i * 4];
				const double        g = gem.px[i] / 255.0;
				const auto          m = AltarMetal(p, a_metal);
				for (int k = 0; k < 3; ++k) result.px[i * 4 + static_cast<size_t>(k)] = py::Clip8(py::Round(m[static_cast<size_t>(k)] * (1 - g) + p[k] * g));
				result.px[i * 4 + 3] = p[3];
			}
			if (IsBgra(a_stock)) return WriteBgra(a_stock, result);
			return ColourKeepAlpha(a_stock, result);
		}

		// ---- tabs ----

		inline Dds NeedStock(const BakeInputs& a_in, const std::string& a_name)
		{
			Dds out;
			if (!a_in.stock || !a_in.stock(a_name, out)) {
				std::string lower = a_name;
				for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
				throw RecipeSkip{ "KeyError: '" + lower + "'" };
			}
			return out;
		}

		inline Bytes Charcoal(const Dds& a_stock, const std::string& a_preset)
		{
			const auto it = charcoal::Presets().find(a_preset);
			if (it == charcoal::Presets().end()) throw RecipeSkip{ "unknown charcoal preset " + py::Repr(a_preset) };
			if (a_stock.mips > 1) throw RecipeSkip{ "charcoal recolours one mip level; the stock texture has more" };
			const auto r = charcoal::Recolour(Decode(a_stock), it->second);
			if (a_stock.format == PixelFormat::DXT1) return charcoal::NeutralGreyBc1(a_stock.body, r.grey);
			if (a_stock.format != PixelFormat::DXT5) NotBc3();
			return NeutralGreyColour(a_stock.body, py::Values(r.grey), r.grey.width, r.grey.height, false);
		}

		// charcoal_paper_texture: unquantised = the F image's values (float32), else the encoded body
		inline std::vector<double> CharcoalPaperShades(const Dds& a_stock, const std::string& a_preset, const Dds& a_page, double a_tone, double a_blur, Image& a_image)
		{
			const auto it = charcoal::Presets().find(a_preset);
			if (it == charcoal::Presets().end()) throw RecipeSkip{ "unknown charcoal preset " + py::Repr(a_preset) };
			if (a_stock.mips > 1) throw RecipeSkip{ "charcoal_paper recolours one mip level; the stock texture has more" };
			auto page = Lums(Decode(a_page), 128);
			if (page.empty()) page = { 128.0 };
			double       low = Pct(page, 2);
			const double high = Pct(page, 98), middle = page[page.size() / 2];
			a_image = Decode(a_stock);
			const size_t n = Count(a_image);
			auto         own = Lums(a_image, 128);
			if (own.empty()) own = { low };
			low = std::min(low, own[own.size() / 100]);
			Image source = NewImage(Mode::L, a_image.width, a_image.height);
			for (size_t i = 0; i < n; ++i)
				source.px[i] = py::Clip8(py::Round(a_image.px[i * 4 + 3] > 128 ? std::min(high, std::max(low, Lum(&a_image.px[i * 4]))) : middle));
			const Image soft = a_blur != 0 ? GaussianBlur(source, static_cast<float>(a_blur)) : source;
			double      invMiddle = 255 - middle;
			if (invMiddle == 0) invMiddle = 1;
			const auto r = charcoal::Recolour(a_image, it->second);
			std::vector<double> shades(n);
			for (size_t i = 0; i < n; ++i) {
				const double v = soft.px[i], c = r.grey.px[i], w = r.weight.px[i];
				shades[i] = std::max(0.0, std::min(255.0, a_tone * (255 - v) / invMiddle * (1 - w / 255) + c * w / 255));
			}
			return shades;
		}

		inline Bytes CharcoalPaper(const Dds& a_stock, const std::string& a_preset, const Dds& a_page, double a_tone)
		{
			Image      image;
			const auto shades = CharcoalPaperShades(a_stock, a_preset, a_page, a_tone, 1.5, image);
			Image      grey = NewImage(Mode::L, image.width, image.height);
			for (size_t i = 0; i < shades.size(); ++i) grey.px[i] = py::Clip8(py::Round(shades[i]));
			if (a_stock.format == PixelFormat::DXT1) return charcoal::NeutralGreyBc1(a_stock.body, grey);
			if (a_stock.format != PixelFormat::DXT5) NotBc3();
			return NeutralGreyColour(a_stock.body, py::Values(grey), grey.width, grey.height, false);
		}

		inline Bytes FadeBc3Bottom(const Dds& a_stock, const Bytes& a_body, int a_rows, double a_floor)   // fade_bc3_bottom
		{
			Dds faded = a_stock;
			faded.body = a_body;
			const Image alpha = GetChannel(Decode(faded), 3);
			const int   width = alpha.width, height = alpha.height;
			Bytes       out = a_body;
			const int   first = height - a_rows;
			for (int by = py::FloorDiv(first, 4) * 4; by < height; by += 4)
				for (int bx = 0; bx < width; bx += 4) {
					int values[16];
					for (int y = 0; y < 4; ++y)
						for (int x = 0; x < 4; ++x) {
							double t = std::max(0.0, (by + y - first + 1) / static_cast<double>(a_rows));
							t = t * t * (3 - 2 * t);
							if (bx + x >= width || by + y >= height || by + y < 0) throw RecipeSkip{ "IndexError: image index out of range" };
							values[y * 4 + x] = py::RoundI(alpha.px[static_cast<size_t>(by + y) * width + bx + x] * (1 - (1 - a_floor) * t));
						}
					const size_t at = static_cast<size_t>((by / 4) * (width / 4) + bx / 4) * 16;
					std::uint8_t block[8];
					Bc3AlphaBlock(values, block);
					std::memcpy(out.data() + at, block, 8);
				}
			return out;
		}

		inline Bytes QuestTab(const Dds& a_stock, const Dds& a_page, double a_tone)   // quest_tab_texture
		{
			if (a_stock.format == PixelFormat::DXT1) throw RecipeSkip{ "quest tab is expected as BC3" };
			Image      image;
			auto       shades = py::F32(CharcoalPaperShades(a_stock, "quests", a_page, a_tone, 1.5, image));
			const auto glyph = charcoal::Recolour(image, charcoal::Presets().at("quests")).weight;
			Dds        baked = a_page;
			baked.body = InvertPaper(a_page, a_tone, 1.5);
			const Image parchment = Convert(Decode(baked), Mode::L);
			const int   width = image.width, height = image.height;
			const Image profile = Resize(Crop(parchment, { 0, 38, parchment.width, 86 }), width, 1, Resample::Box);
			for (int y = 75; y < height; ++y) {
				double mix = std::min(1.0, (y - 74) / 31.0);
				mix = mix * mix * (3 - 2 * mix);
				for (int x = 0; x < width; ++x) {
					const size_t at = static_cast<size_t>(y) * width + x;
					shades[at] += (profile.px[static_cast<size_t>(x)] - shades[at]) * mix * (1 - glyph.px[at] / 255.0);
				}
			}
			if (a_stock.format != PixelFormat::DXT5) NotBc3();
			return FadeBc3Bottom(a_stock, NeutralGreyFs(a_stock.body, py::F32(shades), width, height), 12, 0.0);
		}

		inline darktab::Style TabStyle(const Values& a_v)
		{
			auto f = [&](const char* k) { return a_v.at(k).v[0]; };
			return { f("selected"), f("unselected"), f("gap"), f("outline"), f("glyph"), f("glyphDim") };
		}

		inline std::vector<Image> GlyphMasks(const BakeInputs& a_in, const std::vector<std::string>& a_names)
		{
			std::vector<Image> masks;
			for (const auto& name : a_names) masks.push_back(GetChannel(Decode(NeedStock(a_in, name)), 3));
			return masks;
		}

		inline Bytes TabAtlas(const Dds& a_stock, int a_active, const Values& a_v, const BakeInputs& a_in, const std::vector<std::string>& a_masks, bool a_magic)
		{
			const auto masks = GlyphMasks(a_in, a_masks);
			if (masks.size() < 5) throw RecipeSkip{ "IndexError: list index out of range" };
			const Image image = Decode(a_stock);
			Bytes       encoded;
			Bytes       spliced = SpliceColour(a_stock, darktab::DarkTab(a_active, image, masks, TabStyle(a_v), a_magic), &encoded);
			if (!a_magic) return spliced;
			const int rows = darktab::kStrip + 4;
			for (int block = 0; block < ((rows + 3) / 4) * (a_stock.width / 4); ++block) {
				const size_t at = static_cast<size_t>(block) * 16;
				std::memcpy(spliced.data() + at, encoded.data() + at, 8);
			}
			return spliced;
		}

		inline constexpr int kTabIcon = 76, kTabIconTop = 12, kTabIconShift = -2;

		inline int Dither565(double a_value, int a_x, int a_y)
		{
			const double v = std::max(0.0, std::min(255.0, a_value));
			const auto&  levels = Grey5();
			int          k = std::min(30, std::max(0, static_cast<int>(std::floor(v / 8.2258))));
			while (k < 30 && levels[static_cast<size_t>(k + 1)] <= v) ++k;
			while (k > 0 && levels[static_cast<size_t>(k)] > v) --k;
			const double low = levels[static_cast<size_t>(k)], high = levels[static_cast<size_t>(k + 1)];
			return (v - low) / (high - low) > (kBayer4[(a_y % 4) * 4 + a_x % 4] + 0.5) / 16 ? static_cast<int>(high) : static_cast<int>(low);
		}

		inline Bytes TabPaper(const Dds& a_stock, const std::string& a_name, const Values& a_v, const BakeInputs& a_in, const PackData& a_data)
		{
			const Image image = Decode(a_stock);
			const int   width = image.width, height = image.height;
			std::string lower = a_name;
			for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			const auto  key = lower.rfind("t_magic_tabs", 0) == 0 ? "magic_tab" : "tab";
			const auto  it = a_data.glyphMasks.find(key);
			const auto  names = it == a_data.glyphMasks.end() ? std::vector<std::string>{} : it->second;
			if (!a_in.stock || names.size() != 5) throw RecipeSkip{ "tab_paper needs the five clean category masks" };
			Image inside = NewImage(Mode::L, width, height);
			for (int i = 0; i < 5; ++i) {
				const Image mask = Resize(GetChannel(Decode(NeedStock(a_in, names[static_cast<size_t>(i)])), 3), kTabIcon, kTabIcon, Resample::Bilinear);
				std::array<std::uint8_t, 256> lut{};
				for (int a = 0; a < 256; ++a) lut[static_cast<size_t>(a)] = a > 128 ? 255 : 0;
				Paste(inside, Point(mask, lut), darktab::kCenters[i] + kTabIconShift - kTabIcon / 2, kTabIconTop);
			}
			const Image near = MaxFilter(inside, 2 * 3 + 1);
			const auto& dark = a_v.at("tabPaperDark").v;
			const auto& light = a_v.at("tabPaperLight").v;
			const auto& glyph = a_v.at("tabGlyph").v;
			Image       out = NewImage(Mode::RGBA, width, height);
			for (int y = 0; y < height; ++y)
				for (int x = 0; x < width; ++x) {
					const std::uint8_t* p = image.At(x, y);
					const double        l = Lum601(p);
					const double        t = (l - 80.0) / (210.0 - 80.0);
					double              paper[3];
					for (int i = 0; i < 3; ++i) paper[i] = dark[static_cast<size_t>(i)] + (light[static_cast<size_t>(i)] - dark[static_cast<size_t>(i)]) * t;
					double start = -1;
					bool   inGlyphBox = false;
					for (int c : darktab::kCenters)
						if (-42 <= x - c && x - c <= 36) inGlyphBox = true;
					if (12 <= y && y <= 88 && inGlyphBox) start = inside.px[static_cast<size_t>(y) * width + x] ? 0.22 : (near.px[static_cast<size_t>(y) * width + x] ? 0.24 : -1);
					bool inArrow = false;
					for (int c : darktab::kArrows)
						if (std::abs(x - c) <= 16) inArrow = true;
					if (start < 0 && 36 <= y && y <= 63 && inArrow) start = 0.24;
					double w = 0.0;
					if (start >= 0 && l > 0) w = std::min(1.0, std::max(0.0, ((p[0] - p[2]) / l - start) / 0.06));
					std::uint8_t* d = out.At(x, y);
					for (int i = 0; i < 3; ++i) d[i] = static_cast<std::uint8_t>(Dither565(paper[i] + (glyph[static_cast<size_t>(i)] - paper[i]) * w, x, y));
					d[3] = 255;
				}
			return SpliceColour(a_stock, out);
		}

		struct StripZones
		{
			const char* name;
			Box         zones[3];
		};
		inline constexpr StripZones kTabStripArt[3] = {
			{ "T_Enchantment_Tab_Gear", { { 90, 34, 125, 69 }, { 168, 9, 239, 94 }, { 281, 33, 317, 69 } } },
			{ "T_Enchantment_Tab_SoulGem", { { 399, 33, 434, 69 }, { 483, 13, 545, 90 }, { 591, 34, 625, 69 } } },
			{ "T_Enchantment_Tab_Effect", { { 708, 34, 742, 69 }, { 780, 9, 862, 93 }, { 899, 34, 934, 69 } } },
		};

		inline Bytes TabStrip(const Dds& a_stock, const std::array<double, 3>& a_icon, const std::array<double, 3>& a_paper, const BakeInputs& a_in)
		{
			const Image         image = Decode(a_stock);
			const size_t        n = Count(image);
			std::vector<double> weights(n);
			if (a_in.stock) {
				Image mask = NewImage(Mode::L, image.width, image.height);
				if (image.width != 1024 || image.height != 128) throw RecipeSkip{ "the stock tab strip has unexpected dimensions" };
				for (const auto& art : kTabStripArt) {
					const Image source = Decode(NeedStock(a_in, art.name));
					if (source.width != image.width || source.height != image.height) throw RecipeSkip{ "the stock tab strips have different dimensions" };
					const Image local = MedianFilter(Convert(source, Mode::L), 41);
					for (const auto& zone : art.zones) {
						const Image pixels = Crop(source, zone), paper = Crop(local, zone);
						Image       strokes = NewImage(Mode::L, zone.x1 - zone.x0, zone.y1 - zone.y0);
						for (size_t i = 0; i < strokes.px.size(); ++i)
							strokes.px[i] = py::Clip8(py::Round(255 * Clamp01((paper.px[i] - Lum(&pixels.px[i * 4]) - 11) / 24)));
						Paste(mask, strokes, zone.x0, zone.y0);
					}
				}
				for (size_t i = 0; i < n; ++i) weights[i] = mask.px[i] / 255.0;
			} else {
				const Image gray = Convert(image, Mode::L);
				const Image local = MedianFilter(gray, 15);
				for (size_t i = 0; i < n; ++i) weights[i] = Clamp01((local.px[i] - gray.px[i] - 12) / 25.0);
			}
			Image result = NewImage(Mode::RGBA, image.width, image.height);
			for (size_t i = 0; i < n; ++i) {
				const std::uint8_t* p = &image.px[i * 4];
				const double        shade = (8 + 0.10 * Lum(p)) / 27, w = weights[i];
				for (int k = 0; k < 3; ++k)
					result.px[i * 4 + static_cast<size_t>(k)] = py::Clip8(std::min(255.0, py::Round(a_icon[static_cast<size_t>(k)] * w + a_paper[static_cast<size_t>(k)] * shade * (1 - w))));
				result.px[i * 4 + 3] = p[3];
			}
			return a_stock.format == PixelFormat::DXT1 ? ColourBc1(a_stock, result) : ColourKeepAlpha(a_stock, result);
		}

		// ---- art ----

		inline Colour RoundedColour(const model::Value& a_v, std::uint8_t a_alpha)
		{
			return { py::Clip8(py::Round(a_v.v[0])), py::Clip8(py::Round(a_v.v[1])), py::Clip8(py::Round(a_v.v[2])), a_alpha };
		}

		inline Bytes QuestIcon(const Dds& a_stock, const model::Value& a_blue, const model::Value& a_gold)
		{
			if (a_stock.mips > 1) throw RecipeSkip{ "quest icon has more than one mip" };
			const Image image = Decode(a_stock);
			const Image coverage = GetChannel(image, 3);
			if (GetExtrema(coverage) == std::pair<std::uint8_t, std::uint8_t>{ 255, 255 }) throw RecipeSkip{ "quest icon is opaque artwork" };
			const Image composed = Composite(NewImage(Mode::RGBA, image.width, image.height, RoundedColour(a_blue, 255)),
				NewImage(Mode::RGBA, image.width, image.height, RoundedColour(a_gold, 255)), coverage);
			if (IsBgra(a_stock)) return ToRaw(composed, "BGRA");
			Bytes out = SpliceColour(a_stock, composed);
			for (size_t offset = 0; offset < out.size(); offset += 16) {
				out[offset] = 255;
				out[offset + 1] = 255;
				std::memset(out.data() + offset + 2, 0, 6);
			}
			return out;
		}

		inline Bytes InkGold(const Dds& a_stock, const model::Value& a_gold, const model::Value& a_black)
		{
			const Image image = Decode(a_stock);
			const Image coverage = GetChannel(image, 3);
			std::array<std::uint8_t, 256> on{}, inv{};
			for (int a = 0; a < 256; ++a) {
				on[static_cast<size_t>(a)] = a ? 255 : 0;
				inv[static_cast<size_t>(a)] = static_cast<std::uint8_t>(255 - a);
			}
			const auto box = GetBBox(Point(coverage, on));
			if (!box) throw RecipeSkip{ "icon has no opaque tile" };
			Image composed = Composite(NewImage(Mode::RGBA, image.width, image.height, RoundedColour(a_gold, 255)),
				NewImage(Mode::RGBA, image.width, image.height, RoundedColour(a_black, 255)), Point(coverage, inv));
			Image alpha = NewImage(Mode::L, image.width, image.height, { 0 });
			PasteFill(alpha, *box, { 255 });
			PutAlpha(composed, alpha);
			return FullRgba(a_stock, composed);
		}

		inline Bytes WhiteBacking(const Dds& a_stock)
		{
			const Image image = Decode(a_stock);
			return FullRgba(a_stock, AlphaComposite(NewImage(Mode::RGBA, image.width, image.height, { 255, 255, 255, 255 }), image));
		}

		inline Image SkillRingImage(const Image& a_image)
		{
			static constexpr int    stopAt[5] = { 40, 110, 150, 190, 235 };
			static constexpr double stopRgb[5][3] = { { 36, 24, 8 }, { 104, 72, 26 }, { 156, 114, 44 }, { 206, 164, 80 }, { 246, 220, 150 } };
			std::vector<std::array<double, 3>> ramp(706, { stopRgb[0][0], stopRgb[0][1], stopRgb[0][2] });
			for (int s = 0; s < 4; ++s) {
				const int low = stopAt[s], high = stopAt[s + 1];
				for (int total = 3 * low; total < 3 * high; ++total) {
					const double fraction = (total - 3 * low) / static_cast<double>(3 * (high - low));
					for (int k = 0; k < 3; ++k) ramp[static_cast<size_t>(total)][static_cast<size_t>(k)] = stopRgb[s][k] + (stopRgb[s + 1][k] - stopRgb[s][k]) * fraction;
				}
			}
			while (ramp.size() < 766) ramp.push_back({ stopRgb[4][0], stopRgb[4][1], stopRgb[4][2] });
			Image     out = Convert(a_image, Mode::RGB);
			const int width = out.width, height = out.height;
			for (int y = 0; y < height; ++y)
				for (int x = 0; x < width; ++x) {
					std::uint8_t* p = out.At(x, y);
					const double  dx = x - (width - 1) / 2.0, dy = y - (height - 1) / 2.0;
					if (std::sqrt(dx * dx + dy * dy) < width / 2.0) {
						const double sheen = 18 * (1 - (x + y) / static_cast<double>(width + height)) - 9;
						const auto&  c = ramp[static_cast<size_t>(p[0] + p[1] + p[2])];
						for (int k = 0; k < 3; ++k) p[k] = static_cast<std::uint8_t>(static_cast<int>(std::max(0.0, std::min(255.0, c[static_cast<size_t>(k)] + sheen))));
					}
				}
			return out;
		}

		inline Bytes SkillArt(const Dds& a_stock, int a_slot, const model::Value* a_frame, const BakeInputs& a_in)
		{
			const Image image = Decode(a_stock);
			const bool  small = a_frame != nullptr;
			const int   size = a_slot == 0 ? 128 : 64;
			const int   ew = small ? size : 256, eh = small ? size : 512;
			if (image.width != ew || image.height != eh)
				throw RecipeSkip{ "skill art size (" + std::to_string(image.width) + ", " + std::to_string(image.height) + ") does not match its (" + std::to_string(ew) +
					              ", " + std::to_string(eh) + ") illustration mask" };
			if (!(0 <= a_slot && a_slot < 21)) throw RecipeSkip{ "invalid skill art mask slot " + std::to_string(a_slot) };
			const int left = (a_slot % 7) * 256, top = (a_slot / 7) * 640 + (small ? 512 : 0);
			Bytes     png;
			if (!a_in.menusFile || !a_in.menusFile("character-skill-masks.png", png)) throw RecipeSkip{ "could not read skill art mask atlas: [Errno 2] No such file or directory" };
			const auto atlas = DecodePng(png);
			if (!atlas) throw RecipeSkip{ "could not read skill art mask atlas: cannot identify image file" };
			if (atlas->width != 1792 || atlas->height != 1920 || atlas->mode != Mode::LA) throw RecipeSkip{ "unexpected skill art mask atlas size or mode" };
			const auto parts = Split(Crop(*atlas, { left, top, left + image.width, top + image.height }));
			const Image& illustration = parts[0];
			const Image& linework = parts[1];
			if (small) {
				const Image backing = NewImage(Mode::RGB, image.width, image.height, RoundedColour(*a_frame, 0));
				return ColourBc1(a_stock, Composite(Convert(image, Mode::RGB), backing, illustration));
			}
			Image ornament = NewImage(Mode::RGBA, image.width, image.height, { 230, 213, 183, 255 });
			PutAlpha(ornament, linework);
			Image art = image;
			PutAlpha(art, illustration);
			Image card = AlphaComposite(ornament, art);
			PutAlpha(card, Multiply(GetChannel(card, 3), GetChannel(image, 3)));
			return FullRgba(a_stock, card);
		}

		// ---- other ----

		inline Bytes Alchemy(const Dds& a_stock, const std::array<double, 3>& a_metal, const std::array<double, 3>& a_stone)
		{
			auto weight = [](double r, double, double b) { return std::min(1.0, std::max(0.0, ((r - b) - 15) / (35 - 15))); };
			auto brightness = [](double r, double g, double b) { return 0.2126 * py::Linear(r) + 0.7152 * py::Linear(g) + 0.0722 * py::Linear(b); };
			const auto colours = GetColors(Decode(a_stock), 1 << 24);
			double     sums[4] = { 0.0, 0.0, 0.0, 0.0 };
			if (colours)
				for (const auto& cc : *colours) {
					const double r = cc.pixel & 0xFF, g = (cc.pixel >> 8) & 0xFF, b = (cc.pixel >> 16) & 0xFF, a = cc.pixel >> 24, n = cc.count;
					if (a > 128) {
						const double w = weight(r, g, b), v = brightness(r, g, b);
						sums[0] += n * w * v;
						sums[1] += n * w;
						sums[2] += n * (1 - w) * v;
						sums[3] += n * (1 - w);
					}
				}
			const double refMetal = sums[1] != 0 && sums[0] != 0 ? sums[0] / sums[1] : 1.0;
			const double refStone = sums[3] != 0 && sums[2] != 0 ? sums[2] / sums[3] : 1.0;
			const double metalLin[3] = { py::Linear(a_metal[0]), py::Linear(a_metal[1]), py::Linear(a_metal[2]) };
			const double stoneLin[3] = { py::Linear(a_stone[0]), py::Linear(a_stone[1]), py::Linear(a_stone[2]) };
			if (a_stock.format != PixelFormat::DXT5) NotBc3();
			Bytes                                   out = a_stock.body;
			std::map<std::uint32_t, std::uint32_t> mapped;
			for (size_t offset = 8; offset < out.size(); offset += 16)
				for (size_t at : { offset, offset + 2 }) {
					const std::uint32_t e = U16(out, at);
					auto                it = mapped.find(e);
					if (it == mapped.end()) {
						const auto   c = recipes::RgbOf(e);
						const double w = weight(c[0], c[1], c[2]), v = brightness(c[0], c[1], c[2]);
						const double kMetal = v > 0 ? py::Pow(v / refMetal, 0.8) : 0.0;
						const double kStone = v > 0 ? py::Pow(v / refStone, 0.8) : 0.0;
						recipes::Colour rgb;
						for (int k = 0; k < 3; ++k) rgb[static_cast<size_t>(k)] = py::Srgb(w * metalLin[k] * kMetal + (1 - w) * stoneLin[k] * kStone);
						it = mapped.emplace(e, recipes::Rgb565(rgb)).first;
					}
					Put16(out, at, it->second);
				}
			return out;
		}

		inline Bytes EnchantedDot(const Dds& a_stock, const std::array<double, 3>& a_rgb)
		{
			if (a_stock.format != PixelFormat::DXT5) NotBc3();
			Bytes                     out = a_stock.body;
			const int                 width = a_stock.width, height = a_stock.height;
			static constexpr int      levels[8] = { 255, 0, 218, 182, 145, 109, 72, 36 };
			const std::uint32_t       colour = recipes::Rgb565(a_rgb);
			const double              cx = width / 2.0 - 0.5, cy = height / 2.0 - 0.5;
			for (int y = 0; y < height; y += 4)
				for (int x = 0; x < width; x += 4) {
					const size_t  offset = static_cast<size_t>((y / 4) * (width / 4) + x / 4) * 16;
					std::uint64_t bits = 0;
					for (int dy = 0; dy < 4; ++dy)
						for (int dx = 0; dx < 4; ++dx) {
							const double d = py::Pow(py::Pow(x + dx - cx, 2) + py::Pow(y + dy - cy, 2), 0.5);
							const double coverage = std::max(0.0, std::min(1.0, 14.5 - d)) * 255;
							int          best = 0;
							for (int j = 1; j < 8; ++j)
								if (std::fabs(levels[j] - coverage) < std::fabs(levels[best] - coverage)) best = j;
							bits |= std::uint64_t(best) << (3 * (dy * 4 + dx));
						}
					if (offset + 16 > out.size()) throw RecipeSkip{ "the recipe's result has another size" };
					out[offset] = 255;
					out[offset + 1] = 0;
					for (int i = 0; i < 6; ++i) out[offset + 2 + static_cast<size_t>(i)] = static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFF);
					Put16(out, offset + 8, colour);
					Put16(out, offset + 10, colour);
					Put32(out, offset + 12, 0);
				}
			return out;
		}

		inline Bytes StolenMark(const Dds& a_stock, const model::Value& a_rgb, const BakeInputs& a_in)
		{
			Bytes png;
			if (!a_in.menusFile || !a_in.menusFile("Stolen-Item.png", png)) throw RecipeSkip{ "FileNotFoundError: [Errno 2] No such file or directory: 'Stolen-Item.png'" };
			const auto decoded = DecodePng(png);
			if (!decoded) throw RecipeSkip{ "UnidentifiedImageError: cannot identify image file 'Stolen-Item.png'" };
			Image shape = Convert(*decoded, Mode::RGBA);
			if (const auto box = GetBBox(GetChannel(shape, 3))) shape = Crop(shape, *box);
			const int side = std::max(shape.width, shape.height);
			Image     square = NewImage(Mode::RGBA, side, side, { 0, 0, 0, 0 });
			Paste(square, shape, py::FloorDiv(side - shape.width, 2), py::FloorDiv(side - shape.height, 2));
			const Image  alphaSrc = GetChannel(square, 3);
			const Colour colour = RoundedColour(a_rgb, 0);
			Bytes        blocks;
			for (auto [w, h] : MipSizes(a_stock.width, a_stock.height, std::max(1, a_stock.mips))) {
				Image level = NewImage(Mode::RGBA, w, h, colour);
				PutAlpha(level, Resize(alphaSrc, w, h, Resample::Lanczos));
				const Bytes encoded = EncodeBc(level, BcFormat::DXT5);
				blocks.insert(blocks.end(), encoded.begin(), encoded.end());
			}
			if (a_stock.format != PixelFormat::DXT5) NotBc3();
			if (blocks.size() != a_stock.body.size()) throw RecipeSkip{ "AssertionError: size mismatch" };
			return blocks;
		}
	}

	// every texture recipe: the image kinds here, the block/endpoint kinds through BakeTexture (3a). a_name: the asset's name
	// (the stem pipeline reads for the panel masks and tab_paper)
	inline bool BakeTextureAll(std::string_view a_recipe, const std::string& a_name, const Dds& a_stock, const Values& a_values, const PackData& a_data,
		const BakeInputs& a_inputs, Bytes& a_out, std::string& a_skip)
	{
		using namespace img;
		const auto [kind, arg] = Partition(a_recipe);
		auto v = [&](const std::string& k) -> const model::Value& { return a_values.at(k); };
		auto rgb = [&](const std::string& k) {
			const auto& x = v(k);
			return std::array<double, 3>{ x.v[0], x.v[1], x.v[2] };
		};
		auto num = [](std::string_view s) { return recipes::ParseFloat(s); };
		auto tone = [&] { return PaperTone(v("parchmentDark").v[0], v("parchmentLight").v[0], v("panel").v[0]); };
		const auto& stock = a_stock;
		try {
			const auto info = Recipe(a_recipe, a_data);
			for (const auto& k : info.keys)
				if (!a_values.count(k)) throw RecipeSkip{ "KeyError: '" + k + "'" };
			Bytes out;
			if (kind == "paper_scale") out = PaperScale(stock, v("parchmentLight").v[0] * (arg.empty() ? 1.0 : num(arg)));
			else if (kind == "popup_panel") {
				const auto [side, top] = Partition(arg);
				out = PopupPanel(stock, v("parchmentLight"), side.empty() ? 0 : num(side), top.empty() ? 0 : num(top));
			} else if (kind == "popup_paper") {
				const double strength = arg.empty() ? 0.6 : num(arg);
				if (strength == 0) out = PopupPaper(stock, v("parchmentDark").v[0], v("parchmentLight").v[0], { 0, 0, 0 }, 0.0);
				else out = PopupPaper(stock, v("parchmentDark").v[0], v("parchmentLight").v[0], rgb("pattern"), strength);
			} else if (kind == "mask_peak") out = PaperScale(stock, num(arg));
			else if (kind == "smooth_grain") {
				const auto [blur, amount] = Partition(arg);
				const double mid = tone();
				out = SmoothGrain(stock, v("parchmentDark").v[0], v("parchmentLight").v[0], num(blur), num(amount), &mid);
			} else if (kind == "page_grain") {
				const auto [blur, amount] = Partition(arg);
				const double t = tone();
				out = PageGrain(stock, v("frame").v[0], v("panel").v[0], num(blur), num(amount), &t);
			} else if (kind == "paper_invert" || kind == "page_invert") {
				const auto [blur, scale] = Partition(arg);
				const double s = scale.empty() ? 1.0 : num(scale);
				if (kind == "paper_invert") out = InvertPaper(stock, s * tone(), num(blur));
				else out = InvertPaper(stock, s * tone(), num(blur), InvertMode::Frame, v("frame").v[0], v("panel").v[0]);
			} else if (kind == "details_invert") {
				const auto [dark, light] = DetailsBand(stock, v("detailsPage").v[0]);
				out = InvertPaper(stock, (dark + light) / 2.0, num(arg), InvertMode::Shadow);
			} else if (kind == "band_lines") out = BandLines(stock, rgb("pattern"), num(arg));
			else if (kind == "details_smooth") {
				const auto [blur, amount] = Partition(arg);
				const auto [dark, light] = DetailsBand(stock, v("detailsPage").v[0]);
				out = SmoothGrain(stock, dark, light, num(blur), num(amount), nullptr);
			} else if (kind == "gray_smooth") out = GraySmooth(stock, num(arg));
			else if (kind == "line_tint") out = LineTint(stock, rgb("pattern"), num(arg));
			else if (kind == "laurel_tint") {
				const auto [key, reference] = Partition(arg);
				const double ref = reference.empty() ? 0 : num(reference);
				out = LaurelTint(stock, rgb(key.empty() ? std::string("pattern") : std::string(key)), reference.empty() ? nullptr : &ref);
			} else if (kind == "charcoal_paper") {
				if (!a_inputs.stock) throw RecipeSkip{ "charcoal_paper needs the stock page texture" };
				const Dds page = NeedStock(a_inputs, arg == "quests" ? "T_QuestPage_PaperBackground" : "T_Enchantment_Tab_Paper");
				if (arg == "quests") out = QuestTab(stock, page, tone() * 0.7);
				else out = CharcoalPaper(stock, std::string(arg), page, tone());
			} else if (kind == "quest_icon") out = QuestIcon(stock, v("menuTitle"), v("effectIcon"));
			else if (a_recipe == "ink_gold") out = InkGold(stock, v("effectIcon"), v("frame"));
			else if (a_recipe == "white_backing") out = WhiteBacking(stock);
			else if (a_recipe == "skill_ring") out = ColourKeepAlpha(stock, SkillRingImage(Decode(stock)));
			else if (kind == "skill_card") out = SkillArt(stock, static_cast<int>(num(arg)), nullptr, a_inputs);
			else if (kind == "skill_icon") out = SkillArt(stock, static_cast<int>(num(arg)), &v("frame"), a_inputs);
			else if (kind == "charcoal") out = Charcoal(stock, std::string(arg));
			else if (kind == "flat_alpha") {
				const auto [key, scale] = Partition(arg);
				out = FlatAlpha(stock, rgb(std::string(key)), num(scale));
			} else if (kind == "tab" || kind == "magic_tab") {
				const auto it = a_data.glyphMasks.find(std::string(kind));
				const auto names = it == a_data.glyphMasks.end() ? std::vector<std::string>{} : it->second;
				out = TabAtlas(stock, static_cast<int>(num(arg)), a_values, a_inputs, names, kind == "magic_tab");
			} else if (kind == "frame_metal") out = FrameMetal(stock, rgb(std::string(arg)));
			else if (kind == "altar_panel_invert" || kind == "altar_panel") {
				const Dds* mask = nullptr;
				Dds        maskDds;
				if (a_name == "T_Enchantment_EnchantingBackground" && a_inputs.stock) {
					maskDds = NeedStock(a_inputs, "T_Enchantment_Masks");
					mask = &maskDds;
				}
				if (kind == "altar_panel") {
					const auto paper = rgb("altarPaper");
					out = AltarPanel(stock, rgb(std::string(arg)), &paper, mask, 0, nullptr);
				} else {
					const auto [metal, rest] = Partition(arg);
					const auto [blur, rest2] = Partition(rest);
					const auto [clean, stain] = Partition(rest2);
					PanelInvert inv;
					inv.tone = tone();
					inv.blur = blur.empty() ? 0 : num(blur);
					if (!clean.empty() || !stain.empty()) {
						inv.hasClean = true;
						inv.clean = clean.empty() ? 0 : num(clean);
						inv.stain = stain.empty() ? 0 : num(stain);
					}
					out = AltarPanel(stock, rgb(std::string(metal)), nullptr, mask, 0, &inv);
				}
			} else if (a_recipe == "parchment:enchanting") out = Parchment(stock, v("parchmentDark").v[0] * 0.60, v("parchmentLight").v[0] * 0.82);
			else if (a_recipe == "details_page") {
				const double tint = v("detailsPage").v[0];
				std::array<int, 256> table{};
				for (int i = 0; i < 256; ++i) table[static_cast<size_t>(i)] = py::Srgb(py::Linear(i) * tint);
				out = PerPixel(stock, [&](const std::uint8_t* p) { return std::array<double, 3>{ double(table[p[0]]), double(table[p[1]]), double(table[p[2]]) }; });
			} else if (a_recipe == "parchment") out = Parchment(stock, v("parchmentDark").v[0], v("parchmentLight").v[0]);
			else if (a_recipe == "alchemy") out = Alchemy(stock, rgb("alchemyMetal"), rgb("alchemyStone"));
			else if (a_recipe == "tab_strip") out = TabStrip(stock, rgb("enchantingTabIcon"), rgb("enchantingTabPaper"), a_inputs);
			else if (a_recipe == "tab_paper") out = TabPaper(stock, a_name, a_values, a_inputs, a_data);
			else if (a_recipe == "png") {
				Bytes png;
				if (!a_inputs.art || !a_inputs.art(a_name, png)) throw RecipeSkip{ "FileNotFoundError: [Errno 2] No such file or directory" };
				const auto art = DecodePng(png);
				if (!art) throw RecipeSkip{ "UnidentifiedImageError: cannot identify image file" };
				out = ColourKeepAlpha(stock, Convert(*art, Mode::RGBA));
			} else if (a_recipe == "enchanted_dot") out = EnchantedDot(stock, rgb("enchantedDot"));
			else if (a_recipe == "stolen_mark") out = StolenMark(stock, v("stolenMark"), a_inputs);
			else if (a_recipe == "gray")
				out = PerPixel(stock, [](const std::uint8_t* p) {
					const double g = py::Round(Lum(p));
					return std::array<double, 3>{ g, g, g };
				});
			else if (a_recipe == "preview_frame") {
				const auto& dark = v("previewFrameDark").v;
				const auto& light = v("previewFrameLight").v;
				out = PerPixel(stock, [&](const std::uint8_t* p) {
					std::array<double, 3> c{};
					const long long       inv = 255 - static_cast<long long>(py::Round(Lum(p)));
					for (int i = 0; i < 3; ++i) {
						const long long d = static_cast<long long>(py::Round(dark[static_cast<size_t>(i)])), l = static_cast<long long>(py::Round(light[static_cast<size_t>(i)]));
						c[static_cast<size_t>(i)] = static_cast<double>(d + py::FloorDiv((l - d) * inv, 255));
					}
					return c;
				});
			} else
				return BakeTexture(a_recipe, a_stock, a_values, a_out, a_skip);
			if (out.size() != a_stock.body.size())
				throw RecipeSkip{ "the recipe's result has another size (" + std::to_string(out.size()) + " bytes, stock texture " + std::to_string(a_stock.body.size()) + ")" };
			a_out = std::move(out);
			return true;
		} catch (const RecipeSkip& skip) {
			a_skip = skip.reason;
			return false;
		}
	}
}
