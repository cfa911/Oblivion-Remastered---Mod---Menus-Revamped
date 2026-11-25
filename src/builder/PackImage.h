#pragma once
// In-game Pack Builder (Phase 3b): the Pillow image operations the Python Pack Builder uses (tools/pack_builder/pipeline.py,
// charcoal_tabs.py, tools/make_dark_tab_art.py, tools/dds_image.py), ported to C++ with Pillow 13.0.0's integer arithmetic
// so every result is byte-identical (tests/ingame_pack_image_parity_tests.py). Pure: no game, engine or Win32 calls.
// Codecs (BC1/BC3 encode and decode, PNG decode) are in PackImageCodec.h.
//
// What each function ports (Pillow 13.0.0, src/libImaging and src/PIL):
//   Convert          Convert.c (L24 grey: 19595/38470/7471 + 0x8000 >> 16; RGBA<->RGBa premultiply for resize)
//   Resize           Image.resize + Resample.c (LANCZOS/BILINEAR/BOX, 22-bit fixed-point coefficients, horizontal then
//                    vertical pass; RGBA/LA resized premultiplied) and Geometry.c ImagingScaleAffine for NEAREST
//   GaussianBlur     BoxBlur.c (3 passes of the extended box blur, the radius from Gwosdek et al. in float)
//   BoxBlur          BoxBlur.c (one pass)
//   RankFilter       ImageFilter.RankFilter (Filter.c ImagingExpand edge copy) + RankFilter.c; Median/Min/Max filters
//   Composite        Image.composite / Paste.c paste_mask_L (BLEND with DIV255)
//   AlphaComposite   AlphaComposite.c (7 extra bits of precision); AlphaCompositeAt is Image.alpha_composite(im, dest)
//   Multiply/Darker  Chops.c
//   Point, Crop, Paste, PasteFill, PutAlpha, Split/Merge/GetChannel, DrawRectangle (ImageDraw.rectangle fill),
//   GetBBox (alpha_only=True), GetExtrema, GetColors (GetBBox.c ImagingGetColors: the hash table's order)
//
// Pixels are stored packed (L: 1 byte, LA: 2, RGB: 3, RGBA: 4 per pixel), rows top to bottom; that is Pillow's
// tobytes() order, so a buffer compares with Pillow's tobytes() directly.
//
// This file is a port of parts of Pillow, which carries this notice:
//   The Python Imaging Library (PIL) is
//       Copyright (c) 1997-2011 by Secret Labs AB
//       Copyright (c) 1995-2011 by Fredrik Lundh and contributors
//   Pillow is the friendly PIL fork. It is
//       Copyright (c) 2010 by Jeffrey 'Alex' Clark and contributors
//   Like PIL, Pillow is licensed under the open source MIT-CMU License:
//   By obtaining, using, and/or copying this software and/or its associated documentation, you agree that you have read,
//   understood, and will comply with the following terms and conditions:
//   Permission to use, copy, modify and distribute this software and its documentation for any purpose and without fee is
//   hereby granted, provided that the above copyright notice appears in all copies, and that both that copyright notice and
//   this permission notice appear in supporting documentation, and that the name of Secret Labs AB or the author not be
//   used in advertising or publicity pertaining to distribution of the software without specific, written prior permission.
//   SECRET LABS AB AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
//   OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL SECRET LABS AB OR THE AUTHOR BE LIABLE FOR ANY SPECIAL, INDIRECT OR
//   CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
//   CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
//   SOFTWARE.
// The port itself:
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace builder::pack
{
	using Bytes = std::vector<std::uint8_t>;
	enum class Mode : std::uint8_t { L, LA, RGB, RGBA };
	enum class Resample : std::uint8_t { Nearest, Box, Bilinear, Lanczos };
	using Colour = std::array<std::uint8_t, 4>;   // the bands in mode order (L: [0]; LA: [0], [1]; ...)

	struct Image
	{
		Mode mode = Mode::L;
		int width = 0;
		int height = 0;
		Bytes px;   // packed, BandCount(mode) bytes per pixel

		[[nodiscard]] bool Empty() const { return width <= 0 || height <= 0; }
		[[nodiscard]] std::uint8_t* At(int a_x, int a_y) { return px.data() + (static_cast<std::size_t>(a_y) * width + a_x) * Bands(); }
		[[nodiscard]] const std::uint8_t* At(int a_x, int a_y) const { return px.data() + (static_cast<std::size_t>(a_y) * width + a_x) * Bands(); }
		[[nodiscard]] int Bands() const { return mode == Mode::L ? 1 : mode == Mode::LA ? 2 : mode == Mode::RGB ? 3 : 4; }
	};

	struct Box   // Pillow's box: x1, y1 exclusive
	{
		int x0 = 0;
		int y0 = 0;
		int x1 = 0;
		int y1 = 0;
	};

	struct ColourCount   // one getcolors() entry; pixel = band0 | band1 << 8 | band2 << 16 | band3 << 24 (L: the value)
	{
		std::uint32_t count = 0;
		std::uint32_t pixel = 0;
	};

	inline int BandCount(Mode a_mode) { return a_mode == Mode::L ? 1 : a_mode == Mode::LA ? 2 : a_mode == Mode::RGB ? 3 : 4; }
	inline std::string_view RawOrder(Mode a_mode) { return a_mode == Mode::L ? "L" : a_mode == Mode::LA ? "LA" : a_mode == Mode::RGB ? "RGB" : "RGBA"; }

	namespace image_detail
	{
		// Pillow's ImagingUtils.h
		inline std::uint32_t Div255(std::uint32_t a_v) { const std::uint32_t t = a_v + 128; return ((t >> 8) + t) >> 8; }
		inline std::uint8_t MulDiv255(std::uint32_t a_a, std::uint32_t a_b) { const std::uint32_t t = a_a * a_b + 128; return static_cast<std::uint8_t>(((t >> 8) + t) >> 8); }
		inline std::uint8_t Clip8(int a_v) { return static_cast<std::uint8_t>(a_v <= 0 ? 0 : a_v >= 255 ? 255 : a_v); }
		inline std::uint8_t L24(std::uint32_t a_r, std::uint32_t a_g, std::uint32_t a_b) { return static_cast<std::uint8_t>((a_r * 19595 + a_g * 38470 + a_b * 7471 + 0x8000) >> 16); }

		inline int BandOf(Mode a_mode, char a_letter)
		{
			switch (a_mode) {
			case Mode::L: return a_letter == 'L' ? 0 : -1;
			case Mode::LA: return a_letter == 'L' ? 0 : a_letter == 'A' ? 1 : -1;
			case Mode::RGB: return a_letter == 'R' ? 0 : a_letter == 'G' ? 1 : a_letter == 'B' ? 2 : -1;
			default: return a_letter == 'R' ? 0 : a_letter == 'G' ? 1 : a_letter == 'B' ? 2 : a_letter == 'A' ? 3 : -1;
			}
		}

		// Convert.c rgbA2rgba / LA2La (premultiply) and rgba2rgbA / La2LA (back); the colour bands only, alpha last
		inline Image Premultiply(const Image& a_in)
		{
			Image out = a_in;
			const int bands = a_in.Bands();
			for (std::size_t i = 0; i < out.px.size(); i += bands) {
				const std::uint32_t alpha = out.px[i + bands - 1];
				for (int b = 0; b < bands - 1; ++b) out.px[i + b] = MulDiv255(out.px[i + b], alpha);
			}
			return out;
		}

		inline void Unpremultiply(Image& a_img)
		{
			const int bands = a_img.Bands();
			for (std::size_t i = 0; i < a_img.px.size(); i += bands) {
				const int alpha = a_img.px[i + bands - 1];
				if (alpha == 255 || alpha == 0) continue;
				for (int b = 0; b < bands - 1; ++b) a_img.px[i + b] = Clip8((255 * a_img.px[i + b]) / alpha);
			}
		}

		// ---- Resample.c ----
		constexpr int kPrecisionBits = 32 - 8 - 2;
		constexpr double kPi = 3.14159265358979323846;

		inline double BoxFilter(double a_x) { return (a_x > -0.5 && a_x <= 0.5) ? 1.0 : 0.0; }
		inline double BilinearFilter(double a_x)
		{
			if (a_x < 0.0) a_x = -a_x;
			return a_x < 1.0 ? 1.0 - a_x : 0.0;
		}
		inline double SincFilter(double a_x)
		{
			if (a_x == 0.0) return 1.0;
			a_x = a_x * kPi;
			return std::sin(a_x) / a_x;
		}
		inline double LanczosFilter(double a_x) { return (-3.0 <= a_x && a_x < 3.0) ? SincFilter(a_x) * SincFilter(a_x / 3) : 0.0; }

		struct Coeffs
		{
			int ksize = 0;
			std::vector<int> bounds;    // xmin, count per output pixel
			std::vector<std::int32_t> k;   // normalised (normalize_coeffs_8bpc), ksize per output pixel
		};

		inline Coeffs PrecomputeCoeffs(int a_inSize, float a_in0, float a_in1, int a_outSize, Resample a_filter)
		{
			double (*filter)(double) = a_filter == Resample::Box ? BoxFilter : a_filter == Resample::Bilinear ? BilinearFilter : LanczosFilter;
			const double support0 = a_filter == Resample::Box ? 0.5 : a_filter == Resample::Bilinear ? 1.0 : 3.0;
			double filterscale, scale;
			filterscale = scale = static_cast<double>(a_in1 - a_in0) / a_outSize;
			if (filterscale < 1.0) filterscale = 1.0;
			const double support = support0 * filterscale;
			Coeffs c;
			c.ksize = static_cast<int>(std::ceil(support)) * 2 + 1;
			c.bounds.resize(static_cast<std::size_t>(a_outSize) * 2);
			std::vector<double> kk(static_cast<std::size_t>(a_outSize) * c.ksize, 0.0);
			for (int xx = 0; xx < a_outSize; ++xx) {
				const double center = a_in0 + (xx + 0.5) * scale;
				double ww = 0.0;
				const double ss = 1.0 / filterscale;
				int xmin = static_cast<int>(center - support + 0.5);
				if (xmin < 0) xmin = 0;
				int xmax = static_cast<int>(center + support + 0.5);
				if (xmax > a_inSize) xmax = a_inSize;
				xmax -= xmin;
				double* k = &kk[static_cast<std::size_t>(xx) * c.ksize];
				int x = 0;
				for (; x < xmax; ++x) {
					const double w = filter((x + xmin - center + 0.5) * ss);
					k[x] = w;
					ww += w;
				}
				for (x = 0; x < xmax; ++x)
					if (ww != 0.0) k[x] /= ww;
				for (; x < c.ksize; ++x) k[x] = 0;
				c.bounds[xx * 2 + 0] = xmin;
				c.bounds[xx * 2 + 1] = xmax;
			}
			c.k.resize(kk.size());
			for (std::size_t i = 0; i < kk.size(); ++i)
				c.k[i] = kk[i] < 0 ? static_cast<std::int32_t>(-0.5 + kk[i] * (1 << kPrecisionBits)) : static_cast<std::int32_t>(0.5 + kk[i] * (1 << kPrecisionBits));
			return c;
		}

		inline std::uint8_t ClipResample(int a_ss)
		{
			const int v = a_ss >> kPrecisionBits;
			return static_cast<std::uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
		}

		// ImagingResampleInner with the 8bpc passes; Pillow resamples bands independently with the same coefficients, each pass
		// rounding to 8 bits. Pass order (measured on Pillow 13.0.0, 2055 of 2055 random size pairs, every filter): horizontal
		// first, except vertical first when the height shrinks and 2 * (new width - width) > new height - height.
		inline Image ResampleInner(const Image& a_in, int a_w, int a_h, Resample a_filter)
		{
			const float box[4] = { 0.f, 0.f, static_cast<float>(a_in.width), static_cast<float>(a_in.height) };
			const bool needH = a_w != a_in.width || box[0] != 0.f || box[2] != a_w;
			const bool needV = a_h != a_in.height || box[1] != 0.f || box[3] != a_h;
			const auto ch = PrecomputeCoeffs(a_in.width, box[0], box[2], a_w, a_filter);
			auto cv = PrecomputeCoeffs(a_in.height, box[1], box[3], a_h, a_filter);
			const int bands = a_in.Bands();
			// rows a_rowFirst .. of a_src, horizontally
			auto horizontal = [&](const Image& a_src, int a_rowFirst, int a_rows) {
				Image out{ a_in.mode, a_w, a_rows, Bytes(static_cast<std::size_t>(a_w) * a_rows * bands) };
				for (int yy = 0; yy < a_rows; ++yy) {
					const std::uint8_t* row = a_src.At(0, yy + a_rowFirst);
					for (int xx = 0; xx < a_w; ++xx) {
						const int xmin = ch.bounds[xx * 2 + 0], xmax = ch.bounds[xx * 2 + 1];
						const std::int32_t* k = &ch.k[static_cast<std::size_t>(xx) * ch.ksize];
						std::uint8_t* o = out.At(xx, yy);
						for (int b = 0; b < bands; ++b) {
							int ss = 1 << (kPrecisionBits - 1);
							for (int x = 0; x < xmax; ++x) ss += row[(x + xmin) * bands + b] * k[x];
							o[b] = ClipResample(ss);
						}
					}
				}
				return out;
			};
			auto vertical = [&](const Image& a_src) {
				Image out{ a_in.mode, a_src.width, a_h, Bytes(static_cast<std::size_t>(a_src.width) * a_h * bands) };
				for (int yy = 0; yy < a_h; ++yy) {
					const std::int32_t* k = &cv.k[static_cast<std::size_t>(yy) * cv.ksize];
					const int ymin = cv.bounds[yy * 2 + 0], ymax = cv.bounds[yy * 2 + 1];
					for (int xx = 0; xx < a_src.width; ++xx) {
						std::uint8_t* o = out.At(xx, yy);
						for (int b = 0; b < bands; ++b) {
							int ss = 1 << (kPrecisionBits - 1);
							for (int y = 0; y < ymax; ++y) ss += a_src.At(xx, y + ymin)[b] * k[y];
							o[b] = ClipResample(ss);
						}
					}
				}
				return out;
			};
			if (needH && needV && a_h < a_in.height && 2 * (a_w - a_in.width) > a_h - a_in.height)
				return horizontal(vertical(a_in), 0, a_h);
			if (needH) {
				// only the rows the vertical pass reads (ImagingResampleInner's ybox)
				const int yFirst = cv.bounds[0];
				const int yLast = cv.bounds[a_h * 2 - 2] + cv.bounds[a_h * 2 - 1];
				for (int i = 0; i < a_h; ++i) cv.bounds[i * 2] -= yFirst;
				Image tmp = horizontal(a_in, yFirst, yLast - yFirst);
				return needV ? vertical(tmp) : tmp;
			}
			return needV ? vertical(a_in) : a_in;
		}

		// Geometry.c ImagingScaleAffine: nearest neighbour, positions accumulated in double like Pillow
		inline Image ScaleNearest(const Image& a_in, int a_w, int a_h)
		{
			const double a0 = static_cast<double>(static_cast<float>(a_in.width) - 0.f) / a_w;
			const double a4 = static_cast<double>(static_cast<float>(a_in.height) - 0.f) / a_h;
			const int bands = a_in.Bands();
			Image out{ a_in.mode, a_w, a_h, Bytes(static_cast<std::size_t>(a_w) * a_h * bands, 0) };
			auto coord = [](double a_v) { return a_v < 0.0 ? -1 : static_cast<int>(a_v); };
			std::vector<int> xin(a_w, 0);
			int xmin = a_w, xmax = 0;
			double xo = 0.0 + a0 * 0.5;
			for (int x = 0; x < a_w; ++x) {
				const int xi = coord(xo);
				if (xi >= 0 && xi < a_in.width) {
					xmax = x + 1;
					if (x < xmin) xmin = x;
					xin[x] = xi;
				}
				xo += a0;
			}
			double yo = 0.0 + a4 * 0.5;
			for (int y = 0; y < a_h; ++y) {
				const int yi = coord(yo);
				if (yi >= 0 && yi < a_in.height)
					for (int x = xmin; x < xmax; ++x) std::memcpy(out.At(x, y), a_in.At(xin[x], yi), bands);
				yo += a4;
			}
			return out;
		}

		// ---- BoxBlur.c ----
		inline void LineBoxBlur(std::uint8_t* a_out,   // contiguous
			 const std::uint8_t* a_in, int a_stride, int a_lastx, int a_radius, int a_edgeA, int a_edgeB, std::uint32_t a_ww, std::uint32_t a_fw)
		{
			auto in = [&](int a_x) -> std::uint32_t { return a_in[a_x * a_stride]; };
			std::uint32_t acc = in(0) * (a_radius + 1);
			std::uint32_t bulk;
			for (int x = 0; x < a_edgeA - 1; ++x) acc += in(x);
			acc += in(a_lastx) * (a_radius - a_edgeA + 1);
			auto step = [&](int a_x, int a_sub, int a_add, int a_left, int a_right) {
				acc += in(a_add) - in(a_sub);
				bulk = (acc * a_ww) + (in(a_left) + in(a_right)) * a_fw;
				a_out[a_x] = static_cast<std::uint8_t>((bulk + (1u << 23)) >> 24);
			};
			if (a_edgeA <= a_edgeB) {
				for (int x = 0; x < a_edgeA; ++x) step(x, 0, x + a_radius, 0, x + a_radius + 1);
				for (int x = a_edgeA; x < a_edgeB; ++x) step(x, x - a_radius - 1, x + a_radius, x - a_radius - 1, x + a_radius + 1);
				for (int x = a_edgeB; x <= a_lastx; ++x) step(x, x - a_radius - 1, a_lastx, x - a_radius - 1, a_lastx);
			} else {
				for (int x = 0; x < a_edgeB; ++x) step(x, 0, x + a_radius, 0, x + a_radius + 1);
				for (int x = a_edgeB; x < a_edgeA; ++x) step(x, 0, a_lastx, 0, a_lastx);
				for (int x = a_edgeA; x <= a_lastx; ++x) step(x, x - a_radius - 1, a_lastx, x - a_radius - 1, a_lastx);
			}
		}

		// ImagingHorizontalBoxBlur, in place; a_vertical runs it down the columns (Pillow transposes, blurs rows and
		// transposes back, which is the same arithmetic)
		inline void BoxBlurPass(Image& a_img, float a_floatRadius, bool a_vertical)
		{
			const int radius = static_cast<int>(a_floatRadius);
			const std::uint32_t ww = static_cast<std::uint32_t>(static_cast<float>(1 << 24) / (a_floatRadius * 2 + 1));
			const std::uint32_t fw = ((1u << 24) - static_cast<std::uint32_t>(radius * 2 + 1) * ww) / 2;
			const int len = a_vertical ? a_img.height : a_img.width;
			const int lines = a_vertical ? a_img.width : a_img.height;
			const int edgeA = std::min(radius + 1, len);
			const int edgeB = std::max(len - radius - 1, 0);
			const int bands = a_img.Bands();
			const int stride = a_vertical ? a_img.width * bands : bands;
			std::vector<std::uint8_t> line(static_cast<std::size_t>(len));
			for (int l = 0; l < lines; ++l)
				for (int b = 0; b < bands; ++b) {
					std::uint8_t* start = a_vertical ? a_img.At(l, 0) + b : a_img.At(0, l) + b;
					LineBoxBlur(line.data(), start, stride, len - 1, radius, edgeA, edgeB, ww, fw);
					for (int x = 0; x < len; ++x) start[x * stride] = line[x];   // commit, like Pillow's lineOut
				}
		}

		inline float GaussianBoxRadius(float a_radius, int a_passes)
		{
			float sigma2, L, l, a;
			sigma2 = a_radius * a_radius / a_passes;
			L = static_cast<float>(std::sqrt(12.0 * sigma2 + 1.0));
			l = static_cast<float>(std::floor((L - 1.0) / 2.0));
			a = (2 * l + 1) * (l * (l + 1) - 3 * sigma2);
			a /= 6 * (sigma2 - (l + 1) * (l + 1));
			return l + a;
		}

		inline Image BoxBlurPasses(const Image& a_in, float a_xradius, float a_yradius, int a_passes)
		{
			Image out = a_in;
			if (a_xradius != 0)
				for (int i = 0; i < a_passes; ++i) BoxBlurPass(out, a_xradius, false);
			if (a_yradius != 0)
				for (int i = 0; i < a_passes; ++i) BoxBlurPass(out, a_yradius, true);
			return out;
		}
	}

	// ---- creation, raw bytes, modes ----

	// Image.new(mode, size, colour)
	inline Image NewImage(Mode a_mode, int a_w, int a_h, Colour a_colour = {})
	{
		Image img{ a_mode, std::max(a_w, 0), std::max(a_h, 0), {} };
		const int bands = img.Bands();
		img.px.resize(static_cast<std::size_t>(img.width) * img.height * bands);
		for (std::size_t i = 0; i < img.px.size(); i += bands)
			for (int b = 0; b < bands; ++b) img.px[i + b] = a_colour[b];
		return img;
	}

	// Image.frombytes / frombuffer(mode, size, data, "raw", rawmode): rawmode names the bands in byte order ("BGRA", "RGBA",
	// "L", ...); data holds width * height * rawmode.size() bytes
	inline Image FromRaw(Mode a_mode, int a_w, int a_h, const std::uint8_t* a_data, std::string_view a_rawmode)
	{
		Image img = NewImage(a_mode, a_w, a_h);
		const int bands = img.Bands();
		const std::size_t n = static_cast<std::size_t>(img.width) * img.height;
		const std::size_t step = a_rawmode.size();
		for (std::size_t c = 0; c < step; ++c) {
			const int b = image_detail::BandOf(a_mode, a_rawmode[c]);
			if (b < 0) continue;
			for (std::size_t i = 0; i < n; ++i) img.px[i * bands + b] = a_data[i * step + c];
		}
		return img;
	}

	// Image.tobytes("raw", rawmode)
	inline Bytes ToRaw(const Image& a_img, std::string_view a_rawmode)
	{
		const int bands = a_img.Bands();
		const std::size_t n = static_cast<std::size_t>(a_img.width) * a_img.height;
		const std::size_t step = a_rawmode.size();
		Bytes out(n * step, 0);
		for (std::size_t c = 0; c < step; ++c) {
			const int b = image_detail::BandOf(a_img.mode, a_rawmode[c]);
			if (b < 0) continue;
			for (std::size_t i = 0; i < n; ++i) out[i * step + c] = a_img.px[i * bands + b];
		}
		return out;
	}

	// Image.convert(mode) between L, LA, RGB and RGBA (no palette, no transparency key, no dither)
	inline Image Convert(const Image& a_in, Mode a_mode)
	{
		if (a_in.mode == a_mode) return a_in;
		Image out = NewImage(a_mode, a_in.width, a_in.height);
		const int ib = a_in.Bands(), ob = out.Bands();
		const std::size_t n = static_cast<std::size_t>(a_in.width) * a_in.height;
		for (std::size_t i = 0; i < n; ++i) {
			const std::uint8_t* s = &a_in.px[i * ib];
			std::uint8_t* d = &out.px[i * ob];
			std::uint8_t r, g, b, a = 255;
			if (a_in.mode == Mode::L || a_in.mode == Mode::LA) {
				r = g = b = s[0];
				if (a_in.mode == Mode::LA) a = s[1];
			} else {
				r = s[0], g = s[1], b = s[2];
				if (a_in.mode == Mode::RGBA) a = s[3];
			}
			switch (a_mode) {
			case Mode::L: d[0] = (a_in.mode == Mode::LA) ? r : image_detail::L24(r, g, b); break;
			case Mode::LA:
				d[0] = (a_in.mode == Mode::L) ? r : image_detail::L24(r, g, b);
				d[1] = a;
				break;
			case Mode::RGB: d[0] = r, d[1] = g, d[2] = b; break;
			case Mode::RGBA: d[0] = r, d[1] = g, d[2] = b, d[3] = a; break;
			}
		}
		return out;
	}

	// Image.getchannel(band) as an L image
	inline Image GetChannel(const Image& a_in, int a_band)
	{
		Image out = NewImage(Mode::L, a_in.width, a_in.height);
		const int bands = a_in.Bands();
		if (a_band < 0 || a_band >= bands) return out;
		for (std::size_t i = 0; i < out.px.size(); ++i) out.px[i] = a_in.px[i * bands + a_band];
		return out;
	}

	inline std::vector<Image> Split(const Image& a_in)
	{
		std::vector<Image> out;
		for (int b = 0; b < a_in.Bands(); ++b) out.push_back(GetChannel(a_in, b));
		return out;
	}

	// Image.merge(mode, L images of one size); an empty image when they do not fit
	inline Image Merge(Mode a_mode, const std::vector<Image>& a_bands)
	{
		const int bands = BandCount(a_mode);
		if (static_cast<int>(a_bands.size()) != bands) return {};
		for (const auto& b : a_bands)
			if (b.mode != Mode::L || b.width != a_bands[0].width || b.height != a_bands[0].height) return {};
		Image out = NewImage(a_mode, a_bands[0].width, a_bands[0].height);
		for (int b = 0; b < bands; ++b)
			for (std::size_t i = 0; i < a_bands[b].px.size(); ++i) out.px[i * bands + b] = a_bands[b].px[i];
		return out;
	}

	// Image.putalpha(L image): L becomes LA and RGB becomes RGBA first
	inline void PutAlpha(Image& a_img, const Image& a_alpha)
	{
		if (a_img.mode == Mode::L) a_img = Convert(a_img, Mode::LA);
		if (a_img.mode == Mode::RGB) a_img = Convert(a_img, Mode::RGBA);
		if (a_alpha.mode != Mode::L || a_alpha.width != a_img.width || a_alpha.height != a_img.height) return;
		const int bands = a_img.Bands();
		for (std::size_t i = 0; i < a_alpha.px.size(); ++i) a_img.px[i * bands + bands - 1] = a_alpha.px[i];
	}

	// Image.point(256-entry table), the same table on every band
	inline Image Point(const Image& a_in, const std::array<std::uint8_t, 256>& a_lut)
	{
		Image out = a_in;
		for (auto& v : out.px) v = a_lut[v];
		return out;
	}

	// Image.crop(box): outside the image is zero
	inline Image Crop(const Image& a_in, Box a_box)
	{
		Image out = NewImage(a_in.mode, a_box.x1 - a_box.x0, a_box.y1 - a_box.y0);
		const int bands = a_in.Bands();
		for (int y = 0; y < out.height; ++y) {
			const int sy = y + a_box.y0;
			if (sy < 0 || sy >= a_in.height) continue;
			for (int x = 0; x < out.width; ++x) {
				const int sx = x + a_box.x0;
				if (sx >= 0 && sx < a_in.width) std::memcpy(out.At(x, y), a_in.At(sx, sy), bands);
			}
		}
		return out;
	}

	// Image.paste(src, (x, y)) without a mask: copied, clipped to the destination; src is converted to the destination mode
	inline void Paste(Image& a_dst, const Image& a_src, int a_x, int a_y)
	{
		const Image& src = a_src.mode == a_dst.mode ? a_src : Convert(a_src, a_dst.mode);
		const int bands = a_dst.Bands();
		for (int y = 0; y < src.height; ++y) {
			const int dy = y + a_y;
			if (dy < 0 || dy >= a_dst.height) continue;
			for (int x = 0; x < src.width; ++x) {
				const int dx = x + a_x;
				if (dx >= 0 && dx < a_dst.width) std::memcpy(a_dst.At(dx, dy), src.At(x, y), bands);
			}
		}
	}

	// Image.paste(colour, box)
	inline void PasteFill(Image& a_dst, Box a_box, Colour a_colour)
	{
		const int bands = a_dst.Bands();
		for (int y = std::max(a_box.y0, 0); y < std::min(a_box.y1, a_dst.height); ++y)
			for (int x = std::max(a_box.x0, 0); x < std::min(a_box.x1, a_dst.width); ++x) std::memcpy(a_dst.At(x, y), a_colour.data(), bands);
	}

	// ImageDraw.Draw(img).rectangle((x0, y0, x1, y1), fill=colour): both corners inclusive
	inline void DrawRectangle(Image& a_dst, int a_x0, int a_y0, int a_x1, int a_y1, Colour a_colour)
	{
		PasteFill(a_dst, { a_x0, a_y0, a_x1 + 1, a_y1 + 1 }, a_colour);
	}

	// Image.composite(a, b, mask): b where the L mask is 0, a where it is 255, Paste.c BLEND per band between
	inline Image Composite(const Image& a_a, const Image& a_b, const Image& a_mask)
	{
		Image out = a_b;
		if (a_a.mode != a_b.mode || a_a.width != a_b.width || a_a.height != a_b.height || a_mask.mode != Mode::L || a_mask.width != a_b.width ||
			a_mask.height != a_b.height)
			return {};
		const int bands = out.Bands();
		for (std::size_t i = 0; i < a_mask.px.size(); ++i) {
			const std::uint32_t m = a_mask.px[i];
			for (int b = 0; b < bands; ++b) {
				const std::size_t at = i * bands + b;
				out.px[at] = static_cast<std::uint8_t>(image_detail::Div255(out.px[at] * (255 - m) + a_a.px[at] * m));
			}
		}
		return out;
	}

	// Image.alpha_composite(dst, src) of two RGBA images of one size (AlphaComposite.c)
	inline Image AlphaComposite(const Image& a_dst, const Image& a_src)
	{
		if (a_dst.mode != Mode::RGBA || a_src.mode != Mode::RGBA || a_dst.width != a_src.width || a_dst.height != a_src.height) return {};
		constexpr int kBits = 7;
		Image out = a_dst;
		for (std::size_t i = 0; i < out.px.size(); i += 4) {
			const std::uint8_t* s = &a_src.px[i];
			std::uint8_t* o = &out.px[i];
			if (s[3] == 0) continue;
			const std::uint32_t blend = o[3] * (255u - s[3]);
			const std::uint32_t outa255 = s[3] * 255u + blend;
			const std::uint32_t coef1 = s[3] * 255u * 255u * (1u << kBits) / outa255;
			const std::uint32_t coef2 = 255u * (1u << kBits) - coef1;
			for (int c = 0; c < 3; ++c) {
				const std::uint32_t t = s[c] * coef1 + o[c] * coef2 + (0x80u << kBits);
				o[c] = static_cast<std::uint8_t>((((t >> 8) + t) >> 8) >> kBits);
			}
			const std::uint32_t ta = outa255 + 0x80;
			o[3] = static_cast<std::uint8_t>(((ta >> 8) + ta) >> 8);
		}
		return out;
	}

	// Image.alpha_composite(src, dest=(x, y)) in place: the destination box is cropped (zero outside), composited and pasted back
	inline void AlphaCompositeAt(Image& a_dst, const Image& a_src, int a_x, int a_y)
	{
		const Box box{ a_x, a_y, a_x + a_src.width, a_y + a_src.height };
		const Image background = (box.x0 == 0 && box.y0 == 0 && box.x1 == a_dst.width && box.y1 == a_dst.height) ? a_dst : Crop(a_dst, box);
		Paste(a_dst, AlphaComposite(background, a_src), a_x, a_y);
	}

	// ImageChops.multiply / darker (Chops.c): every byte, the smaller common size
	inline Image Multiply(const Image& a_a, const Image& a_b)
	{
		if (a_a.mode != a_b.mode || a_a.width != a_b.width || a_a.height != a_b.height) return {};
		Image out = a_a;
		for (std::size_t i = 0; i < out.px.size(); ++i) out.px[i] = static_cast<std::uint8_t>(int(a_a.px[i]) * int(a_b.px[i]) / 255);
		return out;
	}

	inline Image Darker(const Image& a_a, const Image& a_b)
	{
		if (a_a.mode != a_b.mode || a_a.width != a_b.width || a_a.height != a_b.height) return {};
		Image out = a_a;
		for (std::size_t i = 0; i < out.px.size(); ++i) out.px[i] = std::min(a_a.px[i], a_b.px[i]);
		return out;
	}

	// Image.resize((w, h), filter) with the default box and reducing_gap=None
	inline Image Resize(const Image& a_in, int a_w, int a_h, Resample a_filter)
	{
		if (a_w <= 0 || a_h <= 0 || a_in.Empty()) return NewImage(a_in.mode, std::max(a_w, 0), std::max(a_h, 0));
		if (a_w == a_in.width && a_h == a_in.height) return a_in;
		if (a_filter == Resample::Nearest) return image_detail::ScaleNearest(a_in, a_w, a_h);
		if (a_in.mode == Mode::RGBA || a_in.mode == Mode::LA) {
			Image out = image_detail::ResampleInner(image_detail::Premultiply(a_in), a_w, a_h, a_filter);
			image_detail::Unpremultiply(out);
			return out;
		}
		return image_detail::ResampleInner(a_in, a_w, a_h, a_filter);
	}

	// ImageFilter.GaussianBlur(radius): 3 passes of the extended box blur
	inline Image GaussianBlur(const Image& a_in, float a_radius)
	{
		if (a_radius == 0) return a_in;
		const float r = image_detail::GaussianBoxRadius(a_radius, 3);
		return image_detail::BoxBlurPasses(a_in, r, r, 3);
	}

	// ImageFilter.BoxBlur(radius): one pass
	inline Image BoxBlur(const Image& a_in, float a_radius)
	{
		if (a_radius == 0) return a_in;
		return image_detail::BoxBlurPasses(a_in, a_radius, a_radius, 1);
	}

	// ImageFilter.RankFilter(size, rank): the edge pixels repeated size // 2 out, then the rank-th smallest value of each
	// size x size window (an order statistic, so a sliding histogram gives Pillow's bytes); every band alone. size is odd.
	inline Image RankFilter(const Image& a_in, int a_size, int a_rank)
	{
		if (a_size < 1 || !(a_size & 1) || a_rank < 0 || a_rank >= a_size * a_size) return {};
		const int m = a_size / 2, bands = a_in.Bands();
		Image out = NewImage(a_in.mode, a_in.width, a_in.height);
		auto at = [&](int a_x, int a_y, int a_b) {
			a_x = std::clamp(a_x, 0, a_in.width - 1);
			a_y = std::clamp(a_y, 0, a_in.height - 1);
			return a_in.At(a_x, a_y)[a_b];
		};
		std::array<int, 256> hist;
		for (int b = 0; b < bands; ++b)
			for (int y = 0; y < a_in.height; ++y) {
				hist.fill(0);
				for (int dy = -m; dy <= m; ++dy)
					for (int dx = -m; dx <= m; ++dx) ++hist[at(dx, y + dy, b)];
				for (int x = 0;; ++x) {
					int seen = 0, v = 0;
					for (; v < 256; ++v) {
						seen += hist[v];
						if (seen > a_rank) break;
					}
					out.At(x, y)[b] = static_cast<std::uint8_t>(v);
					if (x + 1 >= a_in.width) break;
					for (int dy = -m; dy <= m; ++dy) {
						--hist[at(x - m, y + dy, b)];
						++hist[at(x + m + 1, y + dy, b)];
					}
				}
			}
		return out;
	}

	inline Image MedianFilter(const Image& a_in, int a_size) { return RankFilter(a_in, a_size, a_size * a_size / 2); }
	inline Image MinFilter(const Image& a_in, int a_size) { return RankFilter(a_in, a_size, 0); }
	inline Image MaxFilter(const Image& a_in, int a_size) { return RankFilter(a_in, a_size, a_size * a_size - 1); }

	// ---- queries ----

	// Image.getbbox() (alpha_only=True): the box of non-zero pixels (the alpha band of LA/RGBA, any band of RGB); none if empty
	inline std::optional<Box> GetBBox(const Image& a_img)
	{
		const int bands = a_img.Bands();
		const bool alphaOnly = a_img.mode == Mode::LA || a_img.mode == Mode::RGBA;
		Box box{ a_img.width, a_img.height, -1, -1 };
		bool any = false;
		for (int y = 0; y < a_img.height; ++y)
			for (int x = 0; x < a_img.width; ++x) {
				const std::uint8_t* p = a_img.At(x, y);
				bool on = false;
				if (alphaOnly)
					on = p[bands - 1] != 0;
				else
					for (int b = 0; b < bands; ++b) on = on || p[b] != 0;
				if (!on) continue;
				any = true;
				box.x0 = std::min(box.x0, x), box.y0 = std::min(box.y0, y);
				box.x1 = std::max(box.x1, x + 1), box.y1 = std::max(box.y1, y + 1);
			}
		if (!any) return std::nullopt;
		return box;
	}

	// Image.getextrema() of band 0 (an L image's (min, max))
	inline std::pair<std::uint8_t, std::uint8_t> GetExtrema(const Image& a_img)
	{
		std::uint8_t lo = 255, hi = 0;
		const int bands = a_img.Bands();
		for (std::size_t i = 0; i < a_img.px.size(); i += bands) lo = std::min(lo, a_img.px[i]), hi = std::max(hi, a_img.px[i]);
		return { lo, hi };
	}

	// Image.getcolors(maxcolors): none when there are more colours. L lists by value (Pillow's histogram); the other modes
	// in the order of Pillow's hash table (GetBBox.c ImagingGetColors), kept sparse here (Pillow allocates the whole table)
	inline std::optional<std::vector<ColourCount>> GetColors(const Image& a_img, int a_maxcolors)
	{
		std::vector<ColourCount> out;
		const int bands = a_img.Bands();
		if (a_img.mode == Mode::L) {
			std::array<std::uint32_t, 256> h{};
			for (auto v : a_img.px) ++h[v];
			for (std::uint32_t v = 0; v < 256; ++v)
				if (h[v]) out.push_back({ h[v], v });
			if (static_cast<int>(out.size()) > a_maxcolors) return std::nullopt;
			return out;
		}
		static constexpr std::uint32_t kSizes[] = { 4, 3, 8, 3, 16, 3, 32, 5, 64, 3, 128, 3, 256, 29, 512, 17, 1024, 9, 2048, 5, 4096, 83, 8192, 27,
			16384, 43, 32768, 3, 65536, 45, 131072, 9, 262144, 39, 524288, 39, 1048576, 9, 2097152, 5, 4194304, 3, 8388608, 33, 16777216, 27,
			33554432, 9, 67108864, 71, 134217728, 39, 268435456, 9, 536870912, 5, 1073741824, 83, 0 };
		std::uint32_t codeSize = 0, codePoly = 0;
		for (int i = 0; kSizes[i]; i += 2)
			if (kSizes[i] > static_cast<std::uint32_t>(a_maxcolors)) {
				codeSize = kSizes[i], codePoly = kSizes[i + 1];
				break;
			}
		if (!codeSize) return std::nullopt;
		const std::uint32_t codeMask = codeSize - 1;
		struct Slot { std::uint32_t pixel; std::uint32_t count; };
		std::unordered_map<std::uint32_t, Slot> table;
		int size = 0;
		const std::size_t n = static_cast<std::size_t>(a_img.width) * a_img.height;
		for (std::size_t p = 0; p < n; ++p) {
			std::uint32_t pixel = 0;
			for (int b = 0; b < bands && b < 3 + (a_img.mode == Mode::RGBA); ++b) pixel |= std::uint32_t(a_img.px[p * bands + b]) << (8 * b);
			if (a_img.mode == Mode::LA) pixel = a_img.px[p * 2] * 0x010101u | std::uint32_t(a_img.px[p * 2 + 1]) << 24;
			std::uint32_t i = (~pixel) & codeMask;
			auto it = table.find(i);
			if (it == table.end()) {
				if (size++ >= a_maxcolors) return std::nullopt;
				table.emplace(i, Slot{ pixel, 1 });
				continue;
			}
			if (it->second.pixel == pixel) {
				++it->second.count;
				continue;
			}
			const std::int32_t sp = static_cast<std::int32_t>(pixel);
			std::uint32_t incr = static_cast<std::uint32_t>(sp ^ (sp >> 3)) & codeMask;
			if (!incr) incr = codeMask;
			for (;;) {
				i = (i + incr) & codeMask;
				auto jt = table.find(i);
				if (jt == table.end()) {
					if (size++ >= a_maxcolors) return std::nullopt;
					table.emplace(i, Slot{ pixel, 1 });
					break;
				}
				if (jt->second.pixel == pixel) {
					++jt->second.count;
					break;
				}
				incr = incr << 1;
				if (incr > codeMask) incr = incr ^ codePoly;
			}
		}
		std::vector<std::pair<std::uint32_t, Slot>> slots(table.begin(), table.end());
		std::sort(slots.begin(), slots.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		for (const auto& [index, slot] : slots) {
			std::uint32_t pixel = slot.pixel;
			if (a_img.mode == Mode::LA) pixel = (pixel & 0xFF) | ((pixel >> 24) << 8);   // reported as (L, A)
			out.push_back({ slot.count, pixel });
		}
		return out;
	}
}
