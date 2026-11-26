#pragma once
// In-game Pack Builder (Phase 3c): tools/pack_builder/charcoal_tabs.py (the charcoal tab recolour and its neutral-grey BC1
// encoder) and the Dark tab compiler of tools/make_dark_tab_art.py (dark_tab), ported to C++ on the Pillow port
// (PackImage.h). Python's arithmetic order, round() (half to even) and CRT pow() are kept, so the results match byte for byte:
// tests/ingame_pack_recipes_tests.cpp, tests/ingame_pack_parity_tests.py.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "builder/ColourModel.h"
#include "builder/PackImage.h"

namespace builder::pack
{
	// a recipe refusing its asset (pipeline.SkipAsset, or another Python exception as "<Type>: <message>")
	struct RecipeSkip
	{
		std::string reason;
	};

	namespace py
	{
		// CPython's float ** and math functions call the CRT; a volatile pointer keeps the compiler from swapping in an
		// intrinsic or a vectorised pow (whose last bit can differ)
		inline double Pow(double a_x, double a_y)
		{
			static double (*volatile fn)(double, double) = &::pow;
			return fn(a_x, a_y);
		}
		inline double Round(double a_v) { return model::PyRound(a_v); }
		inline int    RoundI(double a_v) { return static_cast<int>(model::PyRound(a_v)); }
		inline double Linear(double a_v)   // pipeline.linear
		{
			const double c = a_v / 255;
			return c <= 0.04045 ? c / 12.92 : Pow((c + 0.055) / 1.055, 2.4);
		}
		inline int Srgb(double a_l)   // pipeline.srgb
		{
			a_l = std::max(0.0, std::min(1.0, a_l));
			const double c = a_l <= 0.0031308 ? a_l * 12.92 : 1.055 * Pow(a_l, 1 / 2.4) - 0.055;
			return RoundI(c * 255);
		}
		inline std::uint8_t Clip8(double a_rounded) { return static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, a_rounded))); }
		inline long long FloorDiv(long long a_a, long long a_b)
		{
			long long q = a_a / a_b;
			if ((a_a % a_b != 0) && ((a_a < 0) != (a_b < 0))) --q;
			return q;
		}
		// sum() of floats (CPython 3.12+: Neumaier's compensated summation)
		inline double Sum(const std::vector<double>& a_v)
		{
			double f = 0.0, c = 0.0;
			for (double x : a_v) {
				const double t = f + x;
				if (std::fabs(f) >= std::fabs(x)) c += (f - t) + x;
				else c += (x - t) + f;
				f = t;
			}
			if (c != 0.0 && std::isfinite(c)) f += c;
			return f;
		}
		inline std::string Repr(std::string_view a_s) { return "'" + std::string(a_s) + "'"; }
		// an L image's bytes as Python ints
		inline std::vector<double> Values(const Image& a_l)
		{
			std::vector<double> out(a_l.px.size());
			for (size_t i = 0; i < out.size(); ++i) out[i] = a_l.px[i];
			return out;
		}
		// Image.new("L").putdata(values) for rounded values already in 0..255
		inline Image LFrom(int a_w, int a_h, const std::vector<int>& a_values)
		{
			Image img = NewImage(Mode::L, a_w, a_h);
			for (size_t i = 0; i < img.px.size(); ++i) img.px[i] = static_cast<std::uint8_t>(a_values[i]);
			return img;
		}
		// Image.new("F").putdata(values).getdata(): each value through a float32
		inline std::vector<double> F32(const std::vector<double>& a_v)
		{
			std::vector<double> out(a_v.size());
			for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<double>(static_cast<float>(a_v[i]));
			return out;
		}
	}

	namespace charcoal
	{
		struct Box4
		{
			int x0, y0, x1, y1;
		};

		struct Preset
		{
			std::vector<Box4> iconBoxes;
			bool              faint = true;
			bool              boxRowTest = true;
			std::vector<Box4> patternBoxes;
			bool              glyph = false;
			bool              emblem = false;
		};

		inline const std::map<std::string, Preset>& Presets()   // charcoal_tabs.PRESETS
		{
			static const std::map<std::string, Preset> presets = [] {
				std::vector<Box4> arrows;
				for (int c : { 108, 235, 279, 405, 450, 577, 623, 750, 795, 921 }) arrows.push_back({ c - 12, 36, c + 14, 63 });
				auto with = [&](std::vector<Box4> a_extra) {
					auto out = arrows;
					out.insert(out.end(), a_extra.begin(), a_extra.end());
					return out;
				};
				std::map<std::string, Preset> p;
				p[""] = {};
				p["arrows"] = { arrows, false };
				p["inventory"] = { with({ { 134, 12, 208, 87 }, { 312, 13, 373, 86 }, { 477, 13, 548, 84 }, { 647, 13, 722, 86 }, { 821, 13, 893, 86 } }), false };
				p["magic"] = { with({ { 142, 13, 201, 86 }, { 311, 15, 374, 84 }, { 476, 14, 549, 86 }, { 647, 13, 720, 86 }, { 821, 14, 893, 84 } }), false };
				p["enchanting"] = { { { 94, 38, 120, 65 }, { 168, 9, 239, 94 }, { 286, 38, 312, 65 }, { 403, 38, 430, 65 }, { 483, 13, 545, 90 }, { 595, 38, 621, 65 },
					{ 712, 38, 738, 65 }, { 780, 9, 862, 93 }, { 903, 38, 929, 65 } } };
				p["quests"] = { { { 121, 38, 147, 67 }, { 264, 16, 323, 89 }, { 441, 38, 465, 66 }, { 555, 38, 580, 66 }, { 689, 12, 764, 92 }, { 874, 38, 899, 66 } } };
				Preset glyph;
				glyph.glyph = true;
				p["glyph"] = glyph;
				Preset emblem;
				emblem.emblem = true;
				p["emblem"] = emblem;
				const std::vector<Box4> nowhere = { { -9, -9, -9, -9 } };
				p["paper"] = { nowhere };
				p["laurel"] = { nowhere, true, true, { { 0, 0, 512, 100 } } };
				p["flourish"] = { { { 40, 86, 508, 97 }, { 128, 78, 390, 106 } }, true, false, { { 0, 0, 512, 84 } } };
				p["header_l"] = { { { 16, 138, 500, 150 }, { 130, 124, 390, 166 } }, true, false, { { 0, 0, 512, 132 } } };
				p["header_r"] = { { { 524, 138, 1008, 150 }, { 640, 124, 900, 166 } }, true, false, { { 512, 0, 1024, 132 } } };
				return p;
			}();
			return presets;
		}

		using Rgb3 = std::array<double, 3>;
		inline constexpr Rgb3 kBlack{ 11, 11, 14 }, kCharcoal{ 23, 23, 25 }, kLight{ 41, 41, 45 }, kPrimary{ 242, 240, 235 }, kSecondary{ 216, 213, 208 },
			kInactive{ 133, 133, 137 }, kFaded{ 85, 85, 91 };

		inline double Lum601(double a_r, double a_g, double a_b) { return 0.299 * a_r + 0.587 * a_g + 0.114 * a_b; }
		inline double Lum601(const Rgb3& a_c) { return Lum601(a_c[0], a_c[1], a_c[2]); }
		inline double Smooth(double a_v, double a_lo, double a_hi)
		{
			const double t = std::min(1.0, std::max(0.0, (a_v - a_lo) / (a_hi - a_lo)));
			return t * t * (3 - 2 * t);
		}
		inline double Mix(double a_a, double a_b, double a_t) { return a_a + (a_b - a_a) * a_t; }

		inline const std::array<double, 256>& LinTable()
		{
			static const std::array<double, 256> t = [] {
				std::array<double, 256> out{};
				for (int v = 0; v < 256; ++v) {
					const double c = v / 255.0;
					out[static_cast<size_t>(v)] = c <= 0.04045 ? c / 12.92 : py::Pow((c + 0.055) / 1.055, 2.4);
				}
				return out;
			}();
			return t;
		}
		inline double F(double a_t) { return a_t > 0.008856 ? py::Pow(a_t, 1.0 / 3) : 7.787 * a_t + 16.0 / 116; }

		struct Lab3
		{
			double l, c, b;
		};
		inline Lab3 Lab(std::uint8_t a_r, std::uint8_t a_g, std::uint8_t a_b)   // lab() -> (L*, chroma, b*)
		{
			const auto&  lin = LinTable();
			const double r = lin[a_r], g = lin[a_g], b = lin[a_b];
			const double x = F((0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047);
			const double y = F(0.2126 * r + 0.7152 * g + 0.0722 * b);
			const double z = F((0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883);
			const double A = 500 * (x - y), B = 200 * (y - z);
			return { 116 * y - 16, py::Pow(A * A + B * B, 0.5), B };
		}

		inline double PaperGrey(double a_l)   // paper_grey with the default palette and params
		{
			const double lo = 34.0, mid = 66.0, hi = 80.0;
			const double l = mid + (a_l - mid) * 1.0;
			const double black = Lum601(kBlack), charcoal = Lum601(kCharcoal), light = Lum601(kLight);
			double       v;
			if (l <= mid) {
				v = Mix(black, charcoal, (l - lo) / (mid - lo));
				v = std::max(v, black * 0.45);
			} else {
				v = Mix(charcoal, light, (l - mid) / (hi - mid));
				v = std::min(v, light * 1.25);
			}
			return std::max(0.0, v * 1.0);
		}

		inline double InkGrey(double a_strength, double a_paper)   // ink_grey
		{
			const double s = std::min(1.0, std::max(0.0, a_strength));
			const double t[5] = { 0.0, 0.18, 0.38, 0.68, 1.0 };
			const double v[5] = { a_paper, std::min(255.0, Lum601(kFaded) * 1.0), std::min(255.0, Lum601(kInactive) * 1.0),
				std::min(255.0, Lum601(kSecondary) * 1.0), std::min(255.0, Lum601(kPrimary) * 1.0) };
			for (int i = 0; i < 4; ++i)
				if (s <= t[i + 1]) return Mix(v[i], v[i + 1], (s - t[i]) / (t[i + 1] - t[i]));
			return v[4];
		}

		// _line_median: the median of each pixel's size neighbours along its row or column, edges clamped
		inline std::vector<double> LineMedian(const std::vector<double>& a_values, int a_w, int a_h, int a_size, bool a_horizontal)
		{
			const int           half = a_size / 2;
			std::vector<double> out(a_values.size());
			const int           lines = a_horizontal ? a_h : a_w, length = a_horizontal ? a_w : a_h;
			std::vector<double> line(static_cast<size_t>(length)), window;
			for (int j = 0; j < lines; ++j) {
				auto idx = [&](int i) { return a_horizontal ? static_cast<size_t>(j) * a_w + i : static_cast<size_t>(i) * a_w + j; };
				for (int i = 0; i < length; ++i) line[static_cast<size_t>(i)] = a_values[idx(i)];
				auto at = [&](int i) { return line[static_cast<size_t>(std::min(length - 1, std::max(0, i)))]; };
				window.clear();
				for (int i = -half; i <= half; ++i) window.push_back(at(i));
				std::sort(window.begin(), window.end());
				for (int i = 0; i < length; ++i) {
					out[idx(i)] = window[static_cast<size_t>(half)];
					window.erase(std::lower_bound(window.begin(), window.end(), at(i - half)));
					const double add = at(i + half + 1);
					window.insert(std::upper_bound(window.begin(), window.end(), add), add);
				}
			}
			return out;
		}

		inline Image LocalLight(const Image& a_l)   // _local_light
		{
			const int w = a_l.width, h = a_l.height;
			Image     small = Resize(a_l, std::max(1, w / 2), std::max(1, h / 2), Resample::Bilinear);
			return Resize(RankFilter(small, 31, static_cast<int>(31 * 31 * 0.7)), w, h, Resample::Bilinear);
		}

		struct Result
		{
			Image grey, weight;   // L images
		};

		// recolour(image, **PRESETS[preset]) with the default palette and params
		inline Result Recolour(const Image& a_image, const Preset& a_p)
		{
			const Image rgba = Convert(a_image, Mode::RGBA);
			const int   width = rgba.width, height = rgba.height;
			const size_t n = static_cast<size_t>(width) * height;
			std::vector<double> ls(n), cs(n);
			std::map<std::uint32_t, Lab3> cache;
			for (size_t i = 0; i < n; ++i) {
				const std::uint8_t* p = &rgba.px[i * 4];
				const std::uint32_t key = p[0] | (p[1] << 8) | (p[2] << 16);
				auto it = cache.find(key);
				if (it == cache.end()) it = cache.emplace(key, Lab(p[0], p[1], p[2])).first;
				ls[i] = it->second.l;
				cs[i] = it->second.c;
			}
			auto lFromRounded = [&](auto a_fn) {
				Image img = NewImage(Mode::L, width, height);
				for (size_t i = 0; i < n; ++i) img.px[i] = py::Clip8(py::Round(a_fn(i)));
				return img;
			};
			if (a_p.emblem) {
				const Image cImg = lFromRounded([&](size_t i) { return cs[i] * 6; });
				const Image blurred = GaussianBlur(cImg, 2);
				const double ink = InkGrey(1.0, 0.0);
				Result r{ NewImage(Mode::L, width, height), NewImage(Mode::L, width, height) };
				for (size_t i = 0; i < n; ++i) {
					const double c = blurred.px[i] / 6.0;
					const double fill = std::max(0.0, 30.0 + (ls[i] - 48.0) * 0.9) * 1.0;
					r.grey.px[i] = py::Clip8(py::Round(Mix(fill, ink, Smooth(c, 8.0, 13.0))));
					r.weight.px[i] = static_cast<std::uint8_t>(py::Round(255 * Smooth(c, 8.0, 13.0)));
				}
				return r;
			}
			if (a_p.glyph) {
				const double primary = std::min(255.0, Lum601(kPrimary) * 1.0);
				return { NewImage(Mode::L, width, height, { static_cast<std::uint8_t>(py::Round(primary)) }), NewImage(Mode::L, width, height, { 255 }) };
			}
			const Image lImg = lFromRounded([&](size_t i) { return ls[i] * 2.55; });
			const Image local = LocalLight(lImg);
			std::vector<double> lMed(n), active(n);
			for (size_t i = 0; i < n; ++i) {
				lMed[i] = local.px[i] / 2.55;
				active[i] = 0.5 + (1 - 0.5) * Smooth(lMed[i], 68.0, 77.0);
			}
			const Image cImg = BoxBlur(lFromRounded([&](size_t i) { return cs[i] * 4; }), 1);
			std::vector<double> cAvg(n);
			for (size_t i = 0; i < n; ++i) cAvg[i] = cImg.px[i] / 4.0;
			std::vector<double> lightC;
			for (size_t i = 0; i < n; ++i)
				if (rgba.px[i * 4 + 3] > 200 && ls[i] > 66.0) lightC.push_back(cAvg[i]);
			if (lightC.empty()) lightC = cAvg;
			std::sort(lightC.begin(), lightC.end());
			const double cm = lightC[lightC.size() / 2];
			const auto   rows = LineMedian(ls, width, height, 61, true);
			const auto   cols = LineMedian(ls, width, height, 61, false);
			std::vector<std::uint8_t> boxed(n, 0);
			for (const auto& b : a_p.iconBoxes) {
				const int x0 = b.x0 - 3, y0 = b.y0 - 3, x1 = b.x1 + 3 + 1, y1 = b.y1 + 3 + 1;
				for (int y = std::max(0, y0); y < std::min(height, y1); ++y)
					for (int x = std::max(0, x0); x < std::min(width, x1); ++x) boxed[static_cast<size_t>(y) * width + x] = 1;
			}
			const bool          haveBoxes = !a_p.iconBoxes.empty();
			const auto          wide = haveBoxes && a_p.faint && a_p.boxRowTest ? LineMedian(ls, width, height, 181, true) : lMed;
			std::vector<double> raw(n);
			Image               core = NewImage(Mode::L, width, height);
			for (size_t i = 0; i < n; ++i) {
				const double l = ls[i], c = cAvg[i], lm = lMed[i];
				const double thin = Smooth(std::min({ lm, rows[i], cols[i] }) - l, 3.0, 9.0) * Smooth(c - 0.65 * cm, 0.0, 2.5);
				const double dense = Smooth(lm - l, 3.0, 9.0) * Smooth(c - 1.45 * cm, 0.0, 3.0);
				const double box = a_p.faint && boxed[i] ? Smooth(std::min(lm, wide[i]) - l, 4.0, 10.0) * Smooth(c - 0.75 * cm, 0.0, 2.5) : 0.0;
				double       w = std::max({ thin, dense, box }) * Smooth(rgba.px[i * 4 + 3], 128, 230);
				if (haveBoxes && !boxed[i]) w = 0.0;
				raw[i] = w;
				core.px[i] = w > 0.7 && lm - l > (a_p.faint && boxed[i] ? 8.0 : 14.0) ? 255 : 0;
			}
			const Image         nearImg = MaxFilter(core, 5);
			std::vector<double> weights(n), papers(n);
			for (size_t i = 0; i < n; ++i) {
				weights[i] = nearImg.px[i] ? raw[i] : 0.0;
				const double l = ls[i], lm = lMed[i];
				papers[i] = PaperGrey(lm > l ? l + (lm - l) * weights[i] : l);
			}
			std::vector<std::uint8_t> pattern(n, 0);
			for (const auto& b : a_p.patternBoxes)
				for (int y = std::max(0, b.y0); y < std::min(height, b.y1); ++y)
					for (int x = std::max(0, b.x0); x < std::min(width, b.x1); ++x) pattern[static_cast<size_t>(y) * width + x] = 1;
			std::vector<double> dSoft, cLocal;
			double              patternGrey = 0;
			if (!a_p.patternBoxes.empty()) {
				const Image d = BoxBlur(lFromRounded([&](size_t i) { return (lMed[i] - ls[i]) * 20 + 128; }), 2);
				dSoft.resize(n);
				for (size_t i = 0; i < n; ++i) dSoft[i] = (d.px[i] - 128) / 20.0;
				const Image cl = lFromRounded([&](size_t i) { return cAvg[i] * 8; });
				const Image med = Resize(MedianFilter(Resize(cl, std::max(1, width / 2), std::max(1, height / 2), Resample::Bilinear), 31), width, height, Resample::Bilinear);
				cLocal.resize(n);
				for (size_t i = 0; i < n; ++i) cLocal[i] = med.px[i] / 8.0;
				patternGrey = std::min(255.0, Lum601(kPrimary) * 1.0 * 1.0);
			}
			Result r{ NewImage(Mode::L, width, height), NewImage(Mode::L, width, height) };
			for (size_t i = 0; i < n; ++i) {
				const double l = ls[i], lm = lMed[i], w = weights[i], paper = papers[i];
				const double dark = lm - l;
				double v = w <= 0 ? paper : Mix(paper, InkGrey(std::max(0.30, active[i] * py::Pow(std::min(1.0, std::max(0.0, dark) / 32.0), 1.6)), paper), w);
				if (pattern[i] && w < 0.5) {
					const double greyEnough = std::max(Smooth(1.2 * cm - cAvg[i], 0.0, 2.5), Smooth(0.88 * cLocal[i] - cAvg[i], 0.0, 2.5));
					const double pw = std::min(1.0, std::max(0.0, dSoft[i] - 1.0) / 6.0) * greyEnough;
					if (pw > 0) v = l < lm ? Mix(PaperGrey(lm), patternGrey, pw * 0.8) : v;
				}
				r.grey.px[i] = py::Clip8(py::Round(v));
				r.weight.px[i] = static_cast<std::uint8_t>(py::Round(255 * w));
			}
			return r;
		}

		inline const std::array<int, 32>& Grey5()
		{
			static const std::array<int, 32> g = [] {
				std::array<int, 32> out{};
				for (int k = 0; k < 32; ++k) out[static_cast<size_t>(k)] = (k << 3) | (k >> 2);
				return out;
			}();
			return g;
		}

		// the 5-bit grey ladder rungs around a block's range (k0 low, k1 high)
		inline void Bracket(double a_lo, double a_hi, int& a_k0, int& a_k1)
		{
			const auto& g = Grey5();
			a_k0 = 0;
			for (int k = 0; k < 32; ++k)
				if (g[static_cast<size_t>(k)] <= a_lo) a_k0 = k;
			a_k1 = 31;
			for (int k = 31; k >= 0; --k)
				if (g[static_cast<size_t>(k)] >= a_hi) a_k1 = k;
			if (a_k1 <= a_k0) {
				if (a_k0 < 31) a_k1 = a_k0 + 1;
				else {
					a_k0 = 30;
					a_k1 = 31;
				}
			}
		}
		inline std::uint32_t GreyCode(int a_k) { return (static_cast<std::uint32_t>(a_k) << 11) | (static_cast<std::uint32_t>(2 * a_k) << 5) | static_cast<std::uint32_t>(a_k); }
		inline int Nearest4(const double (&a_palette)[4], double a_v)
		{
			int best = 0;
			for (int j = 1; j < 4; ++j)
				if (std::fabs(a_palette[j] - a_v) < std::fabs(a_palette[best] - a_v)) best = j;
			return best;
		}

		// neutral_grey_bc1: BC1 blocks rewritten as exact neutral greys; throws on a punch-through stock block
		inline Bytes NeutralGreyBc1(const Bytes& a_blocks, const Image& a_grey)
		{
			Bytes     out = a_blocks;
			const int width = a_grey.width, height = a_grey.height;
			size_t    at = 0;
			for (int by = 0; by < height; by += 4)
				for (int bx = 0; bx < width; bx += 4) {
					const std::uint32_t c0 = out[at] | (out[at + 1] << 8), c1 = out[at + 2] | (out[at + 3] << 8);
					const std::uint32_t bits = out[at + 4] | (out[at + 5] << 8) | (out[at + 6] << 16) | (std::uint32_t(out[at + 7]) << 24);
					if (c0 <= c1)
						for (int i = 0; i < 16; ++i)
							if (((bits >> (2 * i)) & 3) == 3) throw RecipeSkip{ "a stock BC1 block uses punch-through transparency" };
					double block[16];
					for (int y = 0; y < 4; ++y)
						for (int x = 0; x < 4; ++x) block[y * 4 + x] = a_grey.px[static_cast<size_t>(std::min(by + y, height - 1)) * width + std::min(bx + x, width - 1)];
					int k0, k1;
					Bracket(*std::min_element(block, block + 16), *std::max_element(block, block + 16), k0, k1);
					const double p0 = Grey5()[static_cast<size_t>(k1)], p1 = Grey5()[static_cast<size_t>(k0)];
					const double palette[4] = { p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3 };
					std::uint32_t index = 0;
					for (int i = 0; i < 16; ++i) index |= static_cast<std::uint32_t>(Nearest4(palette, block[i])) << (2 * i);
					const std::uint32_t e0 = GreyCode(k1), e1 = GreyCode(k0);
					const std::uint32_t words[3] = { e0, e1, index };
					out[at] = static_cast<std::uint8_t>(e0), out[at + 1] = static_cast<std::uint8_t>(e0 >> 8);
					out[at + 2] = static_cast<std::uint8_t>(e1), out[at + 3] = static_cast<std::uint8_t>(e1 >> 8);
					for (int i = 0; i < 4; ++i) out[at + 4 + static_cast<size_t>(i)] = static_cast<std::uint8_t>(words[2] >> (8 * i));
					at += 8;
				}
			return out;
		}
	}

	namespace darktab   // make_dark_tab_art.dark_tab: the Inventory tab compiler
	{
		inline constexpr int kStrip = 98, kSelTop = 4, kBoxTop = 10, kSide = 4, kW = 2, kRadius = 6, kGapHalf = 2, kOverlap = 8, kGlyph = 64;
		inline constexpr int kCenters[5] = { 173, 343, 516, 688, 860 };
		inline constexpr int kEdges[6] = { 0, 260, 432, 603, 774, 1024 };
		inline constexpr int kArrows[10] = { 108, 235, 279, 405, 450, 577, 623, 750, 795, 921 };
		inline constexpr int kMid = 54;   // round((BOX_TOP + STRIP) / 2) = round(54.0)
		inline constexpr int kGlyphTop = kMid - kGlyph / 2;

		struct Style
		{
			double selected, unselected, gap, outline, glyph, glyphDim;
		};

		inline bool RoundedInside(int a_x, int a_y, int a_l, int a_r, int a_t, int a_b)
		{
			if (a_x < a_l || a_x >= a_r || a_y < a_t || a_y >= a_b) return false;
			const int cx = a_x < a_l + kRadius ? a_l + kRadius : (a_x >= a_r - kRadius ? a_r - kRadius - 1 : a_x);
			if (a_y < a_t + kRadius && cx != a_x) {
				const double dx = a_x - cx, dy = a_y - (a_t + kRadius);
				return std::sqrt(dx * dx + dy * dy) <= kRadius;
			}
			return true;
		}

		inline double Shape(int a_active, int a_x, int a_y, const Style& a_v)
		{
			if (a_y >= kStrip) return a_v.selected;
			const int selL = std::max(kSide, kEdges[a_active] - kOverlap), selR = std::min(1024 - kSide, kEdges[a_active + 1] + kOverlap);
			if (RoundedInside(a_x, a_y, selL, selR, kSelTop, kStrip + kW))
				return RoundedInside(a_x, a_y, selL + kW, selR - kW, kSelTop + kW, kStrip + kW) ? a_v.selected : a_v.outline;
			for (int i = 0; i < 5; ++i) {
				if (i == a_active) continue;
				const int l = std::max(kSide, kEdges[i] + kGapHalf), r = std::min(1024 - kSide, kEdges[i + 1] - kGapHalf);
				if (RoundedInside(a_x, a_y, l, r, kBoxTop, kStrip))
					return RoundedInside(a_x, a_y, l + kW, r - kW, kBoxTop + kW, kStrip - kW) ? a_v.unselected : a_v.outline;
			}
			return a_y >= kStrip - kW ? a_v.outline : a_v.gap;
		}

		// masks: the five glyph alpha masks (L, 256 x 256 or larger)
		inline Image DarkTab(int a_active, const Image& a_stock, const std::vector<Image>& a_masks, const Style& a_v, bool a_opaqueStrip)
		{
			if (a_active < 0 || a_active > 4) throw RecipeSkip{ "IndexError: tuple index out of range" };
			const Image alpha = GetChannel(Convert(a_stock, Mode::RGBA), 3);
			Image       out = NewImage(Mode::RGBA, a_stock.width, a_stock.height);
			for (int y = 0; y < a_stock.height; ++y)
				for (int x = 0; x < a_stock.width; ++x) {
					const double base = Shape(a_active, x, y, a_v);
					int          opacity = 0;
					bool         selected = false;
					if (y < kStrip + 4) {
						int index = -1, arrow = -1;
						for (int i = 0; i < 5 && index < 0; ++i)
							if (std::abs(x - kCenters[i]) < 42) index = i;
						for (int i = 0; i < 10 && arrow < 0; ++i)
							if (std::abs(x - kArrows[i]) < 16) arrow = i;
						if (index >= 0) {
							selected = index == a_active;
							const int sx = x - (kCenters[index] - kGlyph / 2), sy = y - kGlyphTop;
							if (0 <= sx && sx < kGlyph && 0 <= sy && sy < kGlyph) {
								const Image& m = a_masks[static_cast<size_t>(index)];
								const int    mx = static_cast<int>((sx + .5) * 256 / kGlyph), my = static_cast<int>((sy + .5) * 256 / kGlyph);
								if (mx >= m.width || my >= m.height) throw RecipeSkip{ "IndexError: image index out of range" };
								opacity = m.px[static_cast<size_t>(my) * m.width + mx];
							}
						} else if (arrow >= 0) {
							const int ax = (x - kArrows[arrow]) * (arrow % 2 == 0 ? 1 : -1);
							const int ay = y - kMid;
							const double hx = ax + 4, hy = ay;
							if (-9 <= ax && ax <= 10 && std::abs(ay) <= (10 - ax) * 0.57 && std::sqrt(hx * hx + hy * hy) > 3) opacity = 255;
							selected = arrow / 2 == a_active;
						}
					}
					const double ink = selected ? a_v.glyph : a_v.glyphDim;
					const auto   g = py::Clip8(py::Round(base + (ink - base) * opacity / 255));
					std::uint8_t* p = out.At(x, y);
					p[0] = p[1] = p[2] = g;
					p[3] = a_opaqueStrip && y < kStrip + 4 ? 255 : alpha.px[static_cast<size_t>(y) * a_stock.width + x];
				}
			return out;
		}
	}
}
