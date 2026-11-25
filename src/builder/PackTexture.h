#pragma once
// In-game Pack Builder (Phase 3a): a stock texture package as retoc to-legacy writes it (.uasset + .uexp [+ .ubulk]), read and
// rewritten in place. Every mip is one FObjectDataResource in the .uasset (inline: offset in the .uexp; flag 1: offset in the
// .ubulk); the mips in table order are the texture's DDS body. Writing back reproduces UE4-DDS-Tools' inject_dds +
// update_package_source(is_official=False) for a same-size texture: the mip bytes, PackageSource "MOD ", every entry's
// OuterIndex 1 and LegacyBulkDataFlags | Size64Bit. KB docs/reverse-engineering/dark-mode-ui.md "Texture mip bytes in a legacy
// uexp". Pure: tests/ingame_pack_core_tests.cpp, tests/ingame_pack_parity_tests.py.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace builder::pack
{
	using Bytes = std::vector<std::uint8_t>;

	enum class PixelFormat { Unknown, DXT1, DXT5, B8G8R8A8, G8 };

	struct MipRef
	{
		bool          inBulk = false;
		std::int64_t  offset = 0;
		std::int64_t  size = 0;
		std::size_t   entryAt = 0;   // the entry's position in the .uasset
	};

	struct TexturePackage
	{
		PixelFormat         format = PixelFormat::Unknown;
		int                 width = 0, height = 0;
		std::vector<MipRef> mips;
		std::size_t         packageSourceAt = 0;
	};

	namespace detail
	{
		template <class T> bool Read(const Bytes& a_b, std::size_t a_at, T& a_out)
		{
			if (a_at > a_b.size() || a_b.size() - a_at < sizeof(T)) return false;
			std::memcpy(&a_out, a_b.data() + a_at, sizeof(T));
			return true;
		}
		template <class T> void Write(Bytes& a_b, std::size_t a_at, T a_v) { std::memcpy(a_b.data() + a_at, &a_v, sizeof(T)); }

		inline constexpr std::uint32_t kTag = 0x9E2A83C1;
		inline constexpr std::uint32_t kSize64Bit = 0x2000;
		inline constexpr std::uint32_t kPayloadAtEndOfFile = 0x1;
		inline constexpr std::uint32_t kUnused = 0x20;
		inline constexpr std::size_t   kEntrySize = 44;
	}

	// the DDS body (every mip, table order) of a texture the recipes read; a_error says why not
	inline bool ParseTexture(const Bytes& a_uasset, const Bytes& a_uexp, const Bytes* a_ubulk, TexturePackage& a_out, std::string& a_error)
	{
		using detail::Read;
		a_out = {};
		auto fail = [&](const char* a_why) {
			a_error = std::string("texture package: ") + a_why;
			return false;
		};
		std::uint32_t tag = 0;
		std::int32_t  version = 0, custom = 0, total = 0, nameLength = 0, generations = 0, dro = 0;
		if (!Read(a_uasset, 0, tag) || tag != detail::kTag) return fail("not a package (.uasset tag)");
		if (!Read(a_uasset, 4, version) || version != -8) return fail("not a UE5 legacy package (file version)");
		if (!Read(a_uasset, 24, custom) || custom != 0) return fail("versioned package (custom versions)");
		if (!Read(a_uasset, 28, total) || total <= 0 || static_cast<std::size_t>(total) > a_uasset.size()) return fail("bad header size");
		if (!Read(a_uasset, 32, nameLength)) return fail("short summary");
		std::size_t p = 36 + (nameLength < 0 ? static_cast<std::size_t>(-nameLength) * 2 : static_cast<std::size_t>(nameLength));
		p += 4;                    // package flags
		p += 8 + 8 + 8 + 16 + 4;   // names, soft objects, gatherable text, exports, imports, depends
		p += 8 + 4 + 4 + 16;       // soft packages, searchable names, thumbnails, guid
		if (!Read(a_uasset, p, generations) || generations <= 0 || generations >= 10) return fail("bad generation count");
		p += 4 + 8 * static_cast<std::size_t>(generations);
		p += 28 + 8;               // engine versions, compression
		const std::size_t source = p;
		p += 4 + 4 + 4 + 4 + 4 + 8 + 8 + 4 + 8;   // package source .. payload toc
		if (!Read(a_uasset, p, dro)) return fail("short summary");
		std::int32_t tableVersion = 0, count = 0;
		if (dro <= 0 || !Read(a_uasset, static_cast<std::size_t>(dro), tableVersion) || tableVersion != 1 ||
			!Read(a_uasset, static_cast<std::size_t>(dro) + 4, count) || count <= 0 || count > 32)
			return fail("no data resource table");
		const std::size_t table = static_cast<std::size_t>(dro) + 8;
		if (table + detail::kEntrySize * static_cast<std::size_t>(count) > a_uasset.size()) return fail("data resource table past the end");
		a_out.packageSourceAt = source;
		for (std::int32_t i = 0; i < count; ++i) {
			const std::size_t at = table + detail::kEntrySize * static_cast<std::size_t>(i);
			std::int64_t      offset = 0, size = 0, raw = 0;
			std::uint32_t     flags = 0;
			Read(a_uasset, at + 4, offset);
			Read(a_uasset, at + 20, size);
			Read(a_uasset, at + 28, raw);
			Read(a_uasset, at + 40, flags);
			if ((flags & detail::kUnused) || size <= 0 || size != raw || offset < 0) return fail("a mip without data");
			MipRef m{ (flags & detail::kPayloadAtEndOfFile) != 0, offset, size, at };
			const Bytes* file = m.inBulk ? a_ubulk : &a_uexp;
			if (!file) return fail("a mip is in the .ubulk, which is missing");
			if (static_cast<std::uint64_t>(offset) + static_cast<std::uint64_t>(size) > file->size()) return fail("a mip past the end of its file");
			a_out.mips.push_back(m);
		}
		static constexpr std::string_view marker = "PF_";
		const auto found = std::search(a_uexp.begin(), a_uexp.end(), marker.begin(), marker.end());
		if (found == a_uexp.end() || found - a_uexp.begin() < 16) return fail("no pixel format in the .uexp");
		const std::size_t k = static_cast<std::size_t>(found - a_uexp.begin());
		std::int32_t      length = 0;
		Read(a_uexp, k - 4, length);
		if (length < 4 || k + static_cast<std::size_t>(length) > a_uexp.size()) return fail("bad pixel format string");
		const std::string format(reinterpret_cast<const char*>(a_uexp.data() + k), static_cast<std::size_t>(length) - 1);
		a_out.format = format == "PF_DXT5" ? PixelFormat::DXT5 : format == "PF_DXT1" ? PixelFormat::DXT1 :
		               format == "PF_B8G8R8A8" ? PixelFormat::B8G8R8A8 : format == "PF_G8" ? PixelFormat::G8 : PixelFormat::Unknown;
		Read(a_uexp, k - 16, a_out.width);
		Read(a_uexp, k - 12, a_out.height);
		return true;
	}

	inline Bytes GatherMips(const TexturePackage& a_tex, const Bytes& a_uexp, const Bytes* a_ubulk)
	{
		Bytes out;
		for (const auto& m : a_tex.mips) {
			const Bytes* file = m.inBulk ? a_ubulk : &a_uexp;
			if (!file || static_cast<std::uint64_t>(m.offset + m.size) > file->size()) return {};
			out.insert(out.end(), file->begin() + m.offset, file->begin() + m.offset + m.size);
		}
		return out;
	}

	// a recoloured body (the same size) written back, with UE4-DDS-Tools' package changes; nothing is written on a refusal
	inline bool InjectMips(const TexturePackage& a_tex, const Bytes& a_body, Bytes& a_uasset, Bytes& a_uexp, Bytes* a_ubulk, std::string& a_error)
	{
		std::int64_t total = 0;
		for (const auto& m : a_tex.mips) {
			const Bytes* file = m.inBulk ? a_ubulk : &a_uexp;
			if (!file || static_cast<std::uint64_t>(m.offset + m.size) > file->size()) {
				a_error = "texture package: a mip past the end of its file";
				return false;
			}
			total += m.size;
		}
		if (a_tex.mips.empty() || total != static_cast<std::int64_t>(a_body.size()) || a_tex.packageSourceAt + 4 > a_uasset.size()) {
			a_error = "the recipe's result has another size (" + std::to_string(a_body.size()) + " bytes, stock texture " + std::to_string(total) + ")";
			return false;
		}
		// UE4-DDS-Tools writes each mip's record (int32 width, height, depth after the inline data) from the DDS mip size list:
		// block-compressed sizes step as max(4, size // 2) and are padded to a multiple of 4, so a 2x2 or 1x1 BC mip becomes
		// 4x4; the stock record holds the real size max(1, top >> level). Checked before anything is written.
		const bool  compressed = a_tex.format == PixelFormat::DXT1 || a_tex.format == PixelFormat::DXT5;
		const int   block = compressed ? 4 : 1;
		std::vector<std::pair<std::size_t, std::pair<int, int>>> records;
		int         w = a_tex.width, h = a_tex.height;
		for (std::size_t level = 0; level < a_tex.mips.size(); ++level) {
			const auto& m = a_tex.mips[level];
			const int   padW = (w + block - 1) / block * block, padH = (h + block - 1) / block * block;
			const int   realW = std::max(1, a_tex.width >> level), realH = std::max(1, a_tex.height >> level);
			if (padW != realW || padH != realH) {
				const std::size_t rec = static_cast<std::size_t>(m.offset + m.size);
				std::int32_t      rw = 0, rh = 0, rd = 0;
				if (m.inBulk || !detail::Read(a_uexp, rec, rw) || !detail::Read(a_uexp, rec + 4, rh) || !detail::Read(a_uexp, rec + 8, rd) ||
					rw != realW || rh != realH || rd != 1) {
					a_error = "texture package: unexpected mip record for mip " + std::to_string(level);
					return false;
				}
				records.push_back({ rec, { padW, padH } });
			}
			w = std::max(block, w / 2);
			h = std::max(block, h / 2);
		}
		for (const auto& [rec, size] : records) {
			detail::Write<std::int32_t>(a_uexp, rec, size.first);
			detail::Write<std::int32_t>(a_uexp, rec + 4, size.second);
		}
		std::int64_t at = 0;
		for (const auto& m : a_tex.mips) {
			Bytes& file = m.inBulk ? *a_ubulk : a_uexp;
			std::memcpy(file.data() + m.offset, a_body.data() + at, static_cast<std::size_t>(m.size));
			at += m.size;
			std::uint32_t flags = 0;
			detail::Read(a_uasset, m.entryAt + 40, flags);
			detail::Write<std::int32_t>(a_uasset, m.entryAt + 36, 1);
			detail::Write<std::uint32_t>(a_uasset, m.entryAt + 40, flags | detail::kSize64Bit);
		}
		std::memcpy(a_uasset.data() + a_tex.packageSourceAt, "MOD ", 4);
		return true;
	}
}
