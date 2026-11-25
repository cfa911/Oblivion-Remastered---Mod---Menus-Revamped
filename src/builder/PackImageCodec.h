#pragma once
// In-game Pack Builder (Phase 3b): the image codecs the Python Pack Builder reaches through Pillow 13.0.0, byte-identical:
//   EncodeBc   Image.save(f, "DDS", pixel_format="DXT1"/"DXT5"): BcnEncode.c, the blocks only (no DDS header). Min/max
//              565 colour endpoints (truncated: r, b >> 3, g >> 2), indices from Pillow's per-band distance, BC3 alpha
//              min/max with the 6-alpha mode (0 and 255 kept exact). Blocks past the image edge are padded like Pillow.
//   DecodeDds  Image.open(dds).convert("RGBA") for BC1 (DXT1, DXGI 70/71) and BC3 (DXT5, DXGI 76/77): BcnDecode.c, the
//              top mip. Anything else (uncompressed, BC7, ...) is refused: the recipes read raw BGRA with FromRaw.
//   DecodePng  Image.open(png) for 8-bit L, LA, RGB and RGBA, not interlaced, no tRNS (the PNGs the builder ships:
//              character-skill-masks.png is LA, Stolen-Item.png and the owner art are RGBA); zlib inflate included.
// Pure: no game, engine or Win32 calls. Pillow's notice: see PackImage.h (this file is a port of parts of Pillow).
// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include "PackImage.h"

namespace builder::pack
{
	enum class BcFormat : std::uint8_t { DXT1, DXT5 };

	namespace codec_detail
	{
		struct Rgb { int c[3]; };

		inline Rgb Decode565(std::uint32_t a_x)
		{
			int r = (a_x & 0xf800) >> 8;
			r |= r >> 5;
			int g = (a_x & 0x7e0) >> 3;
			g |= g >> 6;
			int b = (a_x & 0x1f) << 3;
			b |= b >> 5;
			return { { r, g, b } };
		}

		inline std::uint16_t Encode565(const std::uint8_t* a_c)
		{
			return static_cast<std::uint16_t>(((a_c[0] >> 3) << 11) | ((a_c[1] >> 2) << 5) | (a_c[2] >> 3));
		}

		// a pixel as Pillow's 32-bit layout reads it: L and LA give the grey on bytes 0..2, LA's alpha on byte 3
		inline void Pixel(const Image& a_img, int a_x, int a_y, std::uint8_t* a_out)
		{
			const std::uint8_t* p = a_img.At(a_x, a_y);
			switch (a_img.mode) {
			case Mode::L: a_out[0] = a_out[1] = a_out[2] = p[0], a_out[3] = 255; break;
			case Mode::LA: a_out[0] = a_out[1] = a_out[2] = p[0], a_out[3] = p[1]; break;
			case Mode::RGB: a_out[0] = p[0], a_out[1] = p[1], a_out[2] = p[2], a_out[3] = 255; break;
			case Mode::RGBA: std::memcpy(a_out, p, 4); break;
			}
		}

		// BcnEncode.c encode_bc1_color
		inline void EncodeBc1Colour(const Image& a_img, int a_bx, int a_by, std::uint8_t* a_dst, bool a_separateAlpha)
		{
			std::uint8_t block[16][4] = {};
			std::uint16_t colourMin = 0, colourMax = 0;
			bool first = true, transparency = false;
			for (int i = 0; i < 4; ++i)
				for (int j = 0; j < 4; ++j) {
					std::uint8_t* cur = block[i + j * 4];
					const int x = a_bx + i, y = a_by + j;
					if (x >= a_img.width || y >= a_img.height) {
						cur[0] = cur[1] = cur[2] = 0;   // past the edge (Pillow leaves this pixel's alpha flag unset)
						continue;
					}
					std::uint8_t p[4];
					Pixel(a_img, x, y, p);
					cur[0] = p[0], cur[1] = p[1], cur[2] = p[2];
					if (a_separateAlpha) {
						if (p[3] == 0) {
							cur[3] = 0;
							transparency = true;
							continue;
						}
						cur[3] = 1;
					}
					const std::uint16_t colour = Encode565(cur);
					if (first || colour < colourMin) colourMin = colour;
					if (first || colour > colourMax) colourMax = colour;
					first = false;
				}
			if (transparency) {
				*a_dst++ = static_cast<std::uint8_t>(colourMin);
				*a_dst++ = static_cast<std::uint8_t>(colourMin >> 8);
			}
			*a_dst++ = static_cast<std::uint8_t>(colourMax);
			*a_dst++ = static_cast<std::uint8_t>(colourMax >> 8);
			if (!transparency) {
				*a_dst++ = static_cast<std::uint8_t>(colourMin);
				*a_dst++ = static_cast<std::uint8_t>(colourMin >> 8);
			}
			const Rgb minRgb = Decode565(colourMin), maxRgb = Decode565(colourMax);
			for (int i = 0; i < 4; ++i) {
				std::uint8_t l = 0;
				for (int j = 3; j > -1; --j) {
					const std::uint8_t* cur = block[i * 4 + j];
					if (transparency && !cur[3]) {
						l |= 3 << (j * 2);
						continue;
					}
					float distance = 0;
					int total = 0;
					for (int k = 0; k < 3; ++k) {
						const float denom = static_cast<float>(std::abs(maxRgb.c[k] - minRgb.c[k]));
						if (denom != 0) {
							distance += std::abs(cur[k] - minRgb.c[k]) / denom;
							total += 1;
						}
					}
					if (total == 0) continue;
					if (transparency) {
						distance *= 4 / total;
						if (distance < 1) {
						} else if (distance < 3) {
							l |= 2 << (j * 2);
						} else {
							l |= 1 << (j * 2);
						}
					} else {
						distance *= 6 / total;
						if (distance < 1) {
							l |= 1 << (j * 2);
						} else if (distance < 3) {
							l |= 3 << (j * 2);
						} else if (distance < 5) {
							l |= 2 << (j * 2);
						}
					}
				}
				*a_dst++ = l;
			}
		}

		// BcnEncode.c encode_bc3_alpha
		inline void EncodeBc3Alpha(const Image& a_img, int a_bx, int a_by, std::uint8_t* a_dst)
		{
			std::uint8_t block[16];
			std::uint8_t alphaMin = 0, alphaMax = 0;
			bool first = true;
			for (int i = 0; i < 4; ++i)
				for (int j = 0; j < 4; ++j) {
					const int x = a_bx + i, y = a_by + j;
					if (x >= a_img.width || y >= a_img.height) {
						block[i + j * 4] = 0;
						continue;
					}
					std::uint8_t p[4];
					Pixel(a_img, x, y, p);
					block[i + j * 4] = p[3];
					if (first || p[3] < alphaMin) alphaMin = p[3];
					if (first || p[3] > alphaMax) alphaMax = p[3];
					first = false;
				}
			*a_dst++ = alphaMin;
			*a_dst++ = alphaMax;
			const float denom = static_cast<float>(std::abs(alphaMax - alphaMin));
			for (int i = 0; i < 2; ++i) {
				std::uint32_t l = 0;
				for (int j = 7; j > -1; --j) {
					const std::uint8_t a = block[i * 8 + j];
					if (!a) {
						l |= 6u << (j * 3);
						continue;
					}
					if (a == 255) {
						l |= 7u << (j * 3);
						continue;
					}
					const float distance = denom == 0 ? 0 : std::abs(a - alphaMin) / denom * 10;
					if (distance < 3)
						l |= 2u << (j * 3);
					else if (distance < 5)
						l |= 3u << (j * 3);
					else if (distance < 7)
						l |= 4u << (j * 3);
					else
						l |= 5u << (j * 3);
				}
				*a_dst++ = static_cast<std::uint8_t>(l);
				*a_dst++ = static_cast<std::uint8_t>(l >> 8);
				*a_dst++ = static_cast<std::uint8_t>(l >> 16);
			}
		}

		inline std::uint32_t Le32(const std::uint8_t* a_p) { return a_p[0] | (a_p[1] << 8) | (a_p[2] << 16) | (std::uint32_t(a_p[3]) << 24); }
		inline std::uint32_t Be32(const std::uint8_t* a_p) { return (std::uint32_t(a_p[0]) << 24) | (a_p[1] << 16) | (a_p[2] << 8) | a_p[3]; }

		// BcnDecode.c decode_bc1_color; BC3 colour blocks always decode as 4 colours (separate_alpha)
		inline void DecodeBc1Colour(const std::uint8_t* a_src, std::uint8_t (*a_out)[4], bool a_separateAlpha)
		{
			const std::uint32_t c0 = a_src[0] | (a_src[1] << 8), c1 = a_src[2] | (a_src[3] << 8);
			const std::uint32_t lut = Le32(a_src + 4);
			std::uint8_t p[4][4];
			const Rgb r0 = Decode565(c0), r1 = Decode565(c1);
			for (int k = 0; k < 3; ++k) p[0][k] = static_cast<std::uint8_t>(r0.c[k]), p[1][k] = static_cast<std::uint8_t>(r1.c[k]);
			p[0][3] = p[1][3] = 0xff;
			if (c0 > c1 || a_separateAlpha) {
				for (int k = 0; k < 3; ++k) {
					p[2][k] = static_cast<std::uint8_t>((2 * r0.c[k] + 1 * r1.c[k]) / 3);
					p[3][k] = static_cast<std::uint8_t>((1 * r0.c[k] + 2 * r1.c[k]) / 3);
				}
				p[2][3] = p[3][3] = 0xff;
			} else {
				for (int k = 0; k < 3; ++k) p[2][k] = static_cast<std::uint8_t>((r0.c[k] + r1.c[k]) / 2), p[3][k] = 0;
				p[2][3] = 0xff;
				p[3][3] = 0;
			}
			for (int n = 0; n < 16; ++n) std::memcpy(a_out[n], p[3 & (lut >> (2 * n))], 4);
		}

		// BcnDecode.c decode_bc3_alpha
		inline void DecodeBc3Alpha(const std::uint8_t* a_src, std::uint8_t (*a_out)[4])
		{
			const int a0 = a_src[0], a1 = a_src[1];
			std::uint8_t a[8];
			a[0] = static_cast<std::uint8_t>(a0);
			a[1] = static_cast<std::uint8_t>(a1);
			if (a0 > a1) {
				for (int i = 1; i <= 6; ++i) a[i + 1] = static_cast<std::uint8_t>(((7 - i) * a0 + i * a1) / 7);
			} else {
				for (int i = 1; i <= 4; ++i) a[i + 1] = static_cast<std::uint8_t>(((5 - i) * a0 + i * a1) / 5);
				a[6] = 0;
				a[7] = 0xff;
			}
			const std::uint32_t lut1 = a_src[2] | (a_src[3] << 8) | (a_src[4] << 16);
			const std::uint32_t lut2 = a_src[5] | (a_src[6] << 8) | (a_src[7] << 16);
			for (int n = 0; n < 8; ++n) a_out[n][3] = a[7 & (lut1 >> (3 * n))];
			for (int n = 0; n < 8; ++n) a_out[8 + n][3] = a[7 & (lut2 >> (3 * n))];
		}

		// ---- zlib inflate (RFC 1950/1951) ----
		class Inflater
		{
		public:
			Inflater(const std::uint8_t* a_data, std::size_t a_size) : m_data(a_data), m_size(a_size) {}

			std::optional<Bytes> Run()
			{
				if (m_size < 6) return std::nullopt;
				const int cmf = m_data[0], flg = m_data[1];
				if ((cmf & 15) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 || (flg & 0x20)) return std::nullopt;
				m_pos = 2;
				int last = 0;
				do {
					last = Bits(1);
					const int type = Bits(2);
					bool ok;
					if (type == 0)
						ok = Stored();
					else if (type == 1)
						ok = Fixed();
					else if (type == 2)
						ok = Dynamic();
					else
						ok = false;
					if (!ok || m_bad) return std::nullopt;
				} while (!last);
				m_bitCount = 0;   // the Adler-32 starts at the next byte
				if (m_pos + 4 > m_size) return std::nullopt;
				std::uint32_t s1 = 1, s2 = 0;
				for (auto b : m_out) {
					s1 = (s1 + b) % 65521;
					s2 = (s2 + s1) % 65521;
				}
				if (Be32(m_data + m_pos) != ((s2 << 16) | s1)) return std::nullopt;
				return std::move(m_out);
			}

		private:
			struct Huffman
			{
				std::array<short, 16> count{};
				std::array<short, 320> symbol{};
			};

			int Bits(int a_need)
			{
				int val = m_bitBuf;
				while (m_bitCount < a_need) {
					if (m_pos >= m_size) {
						m_bad = true;
						return 0;
					}
					val |= int(m_data[m_pos++]) << m_bitCount;
					m_bitCount += 8;
				}
				m_bitBuf = val >> a_need;
				m_bitCount -= a_need;
				return val & ((1 << a_need) - 1);
			}

			bool Stored()
			{
				m_bitBuf = 0;
				m_bitCount = 0;
				if (m_pos + 4 > m_size) return false;
				const unsigned len = m_data[m_pos] | (m_data[m_pos + 1] << 8);
				const unsigned nlen = m_data[m_pos + 2] | (m_data[m_pos + 3] << 8);
				m_pos += 4;
				if (len != (~nlen & 0xffff) || m_pos + len > m_size) return false;
				m_out.insert(m_out.end(), m_data + m_pos, m_data + m_pos + len);
				m_pos += len;
				return true;
			}

			int Decode(const Huffman& a_h)
			{
				int code = 0, first = 0, index = 0;
				for (int len = 1; len < 16; ++len) {
					code |= Bits(1);
					if (m_bad) return -1;
					const int count = a_h.count[len];
					if (code - count < first) return a_h.symbol[index + (code - first)];
					index += count;
					first += count;
					first <<= 1;
					code <<= 1;
				}
				return -1;
			}

			static bool Build(Huffman& a_h, const short* a_lengths, int a_n)
			{
				a_h.count.fill(0);
				for (int s = 0; s < a_n; ++s) ++a_h.count[a_lengths[s]];
				if (a_h.count[0] == a_n) return true;   // no codes: only an error if used
				int left = 1;
				for (int len = 1; len < 16; ++len) {
					left <<= 1;
					left -= a_h.count[len];
					if (left < 0) return false;   // over-subscribed
				}
				std::array<short, 16> offs{};
				for (int len = 1; len < 15; ++len) offs[len + 1] = static_cast<short>(offs[len] + a_h.count[len]);
				for (int s = 0; s < a_n; ++s)
					if (a_lengths[s]) a_h.symbol[offs[a_lengths[s]]++] = static_cast<short>(s);
				return true;
			}

			bool Codes(const Huffman& a_lit, const Huffman& a_dist)
			{
				static constexpr short kLenBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
				static constexpr short kLenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
				static constexpr short kDistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
					4097, 6145, 8193, 12289, 16385, 24577 };
				static constexpr short kDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
				for (;;) {
					int symbol = Decode(a_lit);
					if (symbol < 0) return false;
					if (symbol < 256) {
						m_out.push_back(static_cast<std::uint8_t>(symbol));
						continue;
					}
					if (symbol == 256) return true;
					symbol -= 257;
					if (symbol >= 29) return false;
					const int len = kLenBase[symbol] + Bits(kLenExtra[symbol]);
					const int ds = Decode(a_dist);
					if (ds < 0 || ds >= 30) return false;
					const std::size_t dist = static_cast<std::size_t>(kDistBase[ds] + Bits(kDistExtra[ds]));
					if (m_bad || dist > m_out.size()) return false;
					for (int i = 0; i < len; ++i) m_out.push_back(m_out[m_out.size() - dist]);
				}
			}

			bool Fixed()
			{
				Huffman lit, dist;
				short lengths[320];
				int s = 0;
				for (; s < 144; ++s) lengths[s] = 8;
				for (; s < 256; ++s) lengths[s] = 9;
				for (; s < 280; ++s) lengths[s] = 7;
				for (; s < 288; ++s) lengths[s] = 8;
				Build(lit, lengths, 288);
				for (s = 0; s < 30; ++s) lengths[s] = 5;
				Build(dist, lengths, 30);
				return Codes(lit, dist);
			}

			bool Dynamic()
			{
				static constexpr short kOrder[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
				const int nlen = Bits(5) + 257, ndist = Bits(5) + 1, ncode = Bits(4) + 4;
				if (m_bad || nlen > 286 || ndist > 30) return false;
				short lengths[320] = {};
				for (int i = 0; i < ncode; ++i) lengths[kOrder[i]] = static_cast<short>(Bits(3));
				Huffman lencode, distcode;
				if (!Build(lencode, lengths, 19)) return false;
				int index = 0;
				while (index < nlen + ndist) {
					int symbol = Decode(lencode);
					if (symbol < 0) return false;
					if (symbol < 16) {
						lengths[index++] = static_cast<short>(symbol);
						continue;
					}
					short len = 0;
					if (symbol == 16) {
						if (index == 0) return false;
						len = lengths[index - 1];
						symbol = 3 + Bits(2);
					} else if (symbol == 17)
						symbol = 3 + Bits(3);
					else
						symbol = 11 + Bits(7);
					if (index + symbol > nlen + ndist) return false;
					while (symbol--) lengths[index++] = len;
				}
				if (lengths[256] == 0) return false;
				if (!Build(lencode, lengths, nlen) || !Build(distcode, lengths + nlen, ndist)) return false;
				return Codes(lencode, distcode);
			}

			const std::uint8_t* m_data;
			std::size_t m_size;
			std::size_t m_pos = 0;
			int m_bitBuf = 0;
			int m_bitCount = 0;
			bool m_bad = false;
			Bytes m_out;
		};
	}

	// zlib-wrapped deflate data, Adler-32 checked; none on any error
	inline std::optional<Bytes> Inflate(const std::uint8_t* a_data, std::size_t a_size) { return codec_detail::Inflater(a_data, a_size).Run(); }

	// Pillow's DDS save with pixel_format DXT1 or DXT5: the 4x4 blocks, left to right, top to bottom (no header)
	inline Bytes EncodeBc(const Image& a_img, BcFormat a_format)
	{
		Bytes out;
		if (a_img.Empty()) return out;
		const bool hasAlpha = a_img.mode == Mode::RGBA || a_img.mode == Mode::LA;
		const int bw = (a_img.width + 3) / 4, bh = (a_img.height + 3) / 4;
		out.resize(static_cast<std::size_t>(bw) * bh * (a_format == BcFormat::DXT1 ? 8 : 16));
		std::uint8_t* dst = out.data();
		for (int by = 0; by < a_img.height; by += 4)
			for (int bx = 0; bx < a_img.width; bx += 4) {
				if (a_format == BcFormat::DXT5) {
					if (hasAlpha)
						codec_detail::EncodeBc3Alpha(a_img, bx, by, dst);
					else
						std::memset(dst, 0xff, 8);
					dst += 8;
				}
				codec_detail::EncodeBc1Colour(a_img, bx, by, dst, a_format == BcFormat::DXT1 && hasAlpha);
				dst += 8;
			}
		return out;
	}

	// Image.open(dds).convert("RGBA") of a BC1 or BC3 DDS (legacy DXT1/DXT5 or DX10 header): the top mip
	inline std::optional<Image> DecodeDds(const Bytes& a_file)
	{
		using codec_detail::Le32;
		if (a_file.size() < 128 || std::memcmp(a_file.data(), "DDS ", 4) || Le32(&a_file[4]) != 124) return std::nullopt;
		const int height = static_cast<int>(Le32(&a_file[12])), width = static_cast<int>(Le32(&a_file[16]));
		const std::uint32_t pfFlags = Le32(&a_file[80]);
		if (!(pfFlags & 4) || width <= 0 || height <= 0 || width > 16384 || height > 16384) return std::nullopt;
		std::size_t offset = 128;
		bool bc3;
		if (!std::memcmp(&a_file[84], "DXT1", 4))
			bc3 = false;
		else if (!std::memcmp(&a_file[84], "DXT5", 4))
			bc3 = true;
		else if (!std::memcmp(&a_file[84], "DX10", 4) && a_file.size() >= 148) {
			const std::uint32_t dxgi = Le32(&a_file[128]);
			if (dxgi == 70 || dxgi == 71)
				bc3 = false;
			else if (dxgi == 76 || dxgi == 77)
				bc3 = true;
			else
				return std::nullopt;
			offset = 148;
		} else
			return std::nullopt;
		const int bw = (width + 3) / 4, bh = (height + 3) / 4;
		const std::size_t blockSize = bc3 ? 16 : 8;
		if (a_file.size() < offset + static_cast<std::size_t>(bw) * bh * blockSize) return std::nullopt;
		Image img = NewImage(Mode::RGBA, width, height);
		const std::uint8_t* src = &a_file[offset];
		for (int by = 0; by < bh; ++by)
			for (int bx = 0; bx < bw; ++bx, src += blockSize) {
				std::uint8_t px[16][4];
				if (bc3) {
					codec_detail::DecodeBc1Colour(src + 8, px, true);
					codec_detail::DecodeBc3Alpha(src, px);
				} else
					codec_detail::DecodeBc1Colour(src, px, false);
				for (int j = 0; j < 4; ++j)
					for (int i = 0; i < 4; ++i) {
						const int x = bx * 4 + i, y = by * 4 + j;
						if (x < width && y < height) std::memcpy(img.At(x, y), px[j * 4 + i], 4);
					}
			}
		return img;
	}

	// Image.open(png) of an 8-bit, non-interlaced greyscale, grey+alpha, RGB or RGBA PNG without tRNS, in its own mode
	inline std::optional<Image> DecodePng(const Bytes& a_file)
	{
		using codec_detail::Be32;
		static constexpr std::uint8_t kSig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
		if (a_file.size() < 8 || std::memcmp(a_file.data(), kSig, 8)) return std::nullopt;
		std::size_t pos = 8;
		int width = 0, height = 0, colourType = -1;
		Bytes idat;
		bool end = false;
		while (!end) {
			if (pos + 12 > a_file.size()) return std::nullopt;
			const std::uint32_t len = Be32(&a_file[pos]);
			const char* type = reinterpret_cast<const char*>(&a_file[pos + 4]);
			if (len > a_file.size() - pos - 12) return std::nullopt;
			const std::uint8_t* data = &a_file[pos + 8];
			if (!std::memcmp(type, "IHDR", 4)) {
				if (len != 13) return std::nullopt;
				width = static_cast<int>(Be32(data)), height = static_cast<int>(Be32(data + 4));
				if (data[8] != 8 || data[10] != 0 || data[11] != 0 || data[12] != 0) return std::nullopt;   // depth 8, deflate, filter 0, no interlace
				colourType = data[9];
			} else if (!std::memcmp(type, "IDAT", 4))
				idat.insert(idat.end(), data, data + len);
			else if (!std::memcmp(type, "IEND", 4))
				end = true;
			else if (!std::memcmp(type, "tRNS", 4) || !std::memcmp(type, "PLTE", 4))
				return std::nullopt;
			pos += 12 + len;
		}
		Mode mode;
		switch (colourType) {
		case 0: mode = Mode::L; break;
		case 2: mode = Mode::RGB; break;
		case 4: mode = Mode::LA; break;
		case 6: mode = Mode::RGBA; break;
		default: return std::nullopt;
		}
		if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return std::nullopt;
		const auto raw = Inflate(idat.data(), idat.size());
		const int bpp = BandCount(mode);
		const std::size_t stride = static_cast<std::size_t>(width) * bpp;
		if (!raw || raw->size() < (stride + 1) * height) return std::nullopt;
		Image img = NewImage(mode, width, height);
		for (int y = 0; y < height; ++y) {
			const std::uint8_t* in = &(*raw)[y * (stride + 1)];
			const int filter = in[0];
			++in;
			std::uint8_t* row = &img.px[y * stride];
			const std::uint8_t* prev = y ? &img.px[(y - 1) * stride] : nullptr;
			for (std::size_t i = 0; i < stride; ++i) {
				const int a = i >= static_cast<std::size_t>(bpp) ? row[i - bpp] : 0;
				const int b = prev ? prev[i] : 0;
				const int c = (prev && i >= static_cast<std::size_t>(bpp)) ? prev[i - bpp] : 0;
				int p;
				switch (filter) {
				case 0: p = 0; break;
				case 1: p = a; break;
				case 2: p = b; break;
				case 3: p = (a + b) / 2; break;
				case 4:
					{
						const int pa = std::abs(b - c), pb = std::abs(a - c), pc = std::abs(a + b - 2 * c);
						p = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
						break;
					}
				default: return std::nullopt;
				}
				row[i] = static_cast<std::uint8_t>(in[i] + p);
			}
		}
		return img;
	}
}
