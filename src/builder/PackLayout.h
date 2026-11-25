#pragma once
// In-game Pack Builder (Phase 3d): the size/layout half of tools/pack_builder/pipeline.py, byte for byte.
// - [Sizes] (custom_colours.load_sizes) and the manifest's "sizes" (pipeline.used_sizes)
// - bp_float: the layout values set in place after a stock check (floats, Spacer doubles, enum bytes)
// - the package rewrite for values the cook did not store (KB techniques/unversioned-package-rewrite.md): unversioned
//   property headers (parse_header, build_header), add_field, replace_export, rewrite_package with masked_sha256
// - swap_imports (RMI-096): an import's class and names replaced in a legacy package summary (package_summary)
// Every refusal is a false return with the Python SkipAsset text, and leaves its inputs unchanged. A Python failure that is
// not a SkipAsset (struct.error on a truncated package, KeyError) is a refusal here too, with a different text.
// Pure: tests/ingame_pack_layout_tests.cpp, tests/ingame_pack_parity_tests.py.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "builder/ColourModel.h"
#include "builder/PackData.h"
#include "builder/PackSha256.h"
#include "builder/PackTexture.h"

namespace builder::pack
{
	// ---- [Sizes] ----
	struct Sizes   // load_sizes: SIZES order (text role scales, row sizes, font sizes), the file's values over the defaults
	{
		std::vector<std::pair<std::string, double>> items;

		const double* Find(std::string_view a_key) const
		{
			for (const auto& [k, v] : items)
				if (k == a_key) return &v;
			return nullptr;
		}
	};

	// custom_colours.load_sizes: a_text = the colour file (nullptr: no file, today's sizes)
	inline bool LoadSizes(const std::string* a_text, Sizes& a_out, std::string& a_error)
	{
		a_out = {};
		for (const auto& s : model::detail::SizeDefaults()) a_out.items.emplace_back(s.key, s.def);
		if (!a_text) return true;
		std::vector<model::Found> found;
		if (!model::ReadEntries(*a_text, found, a_error)) return false;
		for (const auto& f : found) {
			if (f.section != "Sizes") continue;
			auto it = std::find_if(a_out.items.begin(), a_out.items.end(), [&](const auto& e) { return e.first == f.name; });
			if (it == a_out.items.end()) a_out.items.emplace_back(f.name, f.value.v[0]);
			else it->second = f.value.v[0];
		}
		return true;
	}

	inline json::Value UsedSizes(const Sizes& a_sizes)   // pipeline.used_sizes: every size but the text role scales, as floats
	{
		auto out = json::MakeObject();
		for (const auto& [k, v] : a_sizes.items)
			if (!model::detail::IsTextRole(k)) json::Set(out, k, json::MakeFloat(v));
		return out;
	}

	namespace detail
	{
		inline std::string PyNumber(const json::Value& a_v)   // str() of a JSON number as Python read it
		{
			if (a_v.type == json::Type::Int) return std::to_string(a_v.i);
			if (a_v.type == json::Type::Float) return json::PyRepr(a_v.f);
			return "None";
		}
		template <class T>
		std::string PyList(const T& a_values)   // str() of a list of ints
		{
			std::string s = "[";
			bool        first = true;
			for (const auto& v : a_values) {
				s += (first ? "" : ", ") + std::to_string(v);
				first = false;
			}
			return s + "]";
		}
		inline Bytes Slice(const Bytes& a_b, long long a_from, long long a_to)   // a_b[from:to] (non-negative bounds, clamped)
		{
			const long long n = static_cast<long long>(a_b.size());
			a_from = std::clamp(a_from, 0LL, n);
			a_to = std::clamp(a_to, 0LL, n);
			if (a_to <= a_from) return {};
			return Bytes(a_b.begin() + a_from, a_b.begin() + a_to);
		}
		inline void Append(Bytes& a_out, const Bytes& a_b) { a_out.insert(a_out.end(), a_b.begin(), a_b.end()); }
		template <class T>
		bool ReadAt(const Bytes& a_b, long long a_at, T& a_out)
		{
			if (a_at < 0 || a_at + static_cast<long long>(sizeof(T)) > static_cast<long long>(a_b.size())) return false;
			std::memcpy(&a_out, a_b.data() + a_at, sizeof(T));
			return true;
		}
		template <class T>
		void WriteAt(Bytes& a_b, long long a_at, T a_v) { std::memcpy(a_b.data() + a_at, &a_v, sizeof(T)); }
		template <class T>
		Bytes Pack(T a_v)
		{
			Bytes out(sizeof(T));
			std::memcpy(out.data(), &a_v, sizeof(T));
			return out;
		}
		inline const char* kTruncated = "struct.error: the package ends inside a value";
	}

	// ---- unversioned property headers (UE 5.3) ----
	inline constexpr unsigned kSkipMask = 0x7F, kZeroBit = 0x80, kLastBit = 0x100, kValueShift = 9;

	struct PropertyHeader
	{
		std::vector<int> fields;   // stored field indexes, in order
		std::set<int>    zeroed;   // the zero-masked ones (no value bytes)
		size_t           end = 0;  // the position after the header (where the values start)
	};

	// pipeline.parse_header
	inline bool ParseHeader(const Bytes& a_data, size_t a_pos, PropertyHeader& a_out, std::string& a_error)
	{
		a_out = {};
		struct Frag
		{
			unsigned skip;
			bool     zeroes;
			unsigned values;
		};
		std::vector<Frag> frags;
		size_t            pos = a_pos;
		while (true) {
			std::uint16_t packed = 0;
			if (!detail::ReadAt(a_data, static_cast<long long>(pos), packed)) {
				a_error = detail::kTruncated;
				return false;
			}
			pos += 2;
			frags.push_back({ packed & kSkipMask, (packed & kZeroBit) != 0, static_cast<unsigned>(packed >> kValueShift) });
			if (packed & kLastBit) break;
		}
		std::vector<bool> flags;
		int               index = 0;
		for (const auto& f : frags) {
			index += static_cast<int>(f.skip);
			for (unsigned v = 0; v < f.values; ++v) {
				a_out.fields.push_back(index);
				flags.push_back(f.zeroes);
				++index;
			}
		}
		std::vector<int> masked;
		for (size_t i = 0; i < a_out.fields.size(); ++i)
			if (flags[i]) masked.push_back(a_out.fields[i]);
		if (!masked.empty()) {
			const size_t n = masked.size();
			const size_t size = n <= 8 ? 1 : n <= 16 ? 2 : 4 * ((n + 31) / 32);
			for (size_t i = 0; i < n; ++i) {
				const size_t byte = pos + i / 8;
				if (byte < a_data.size() && (a_data[byte] >> (i % 8) & 1)) a_out.zeroed.insert(masked[i]);
			}
			pos += size;
		}
		a_out.end = pos;
		return true;
	}

	// pipeline.build_header: written like the engine (a fragment is flagged only when one of its own values is zero)
	inline Bytes BuildHeader(std::vector<int> a_fields, const std::set<int>& a_zeroes)
	{
		std::sort(a_fields.begin(), a_fields.end());
		if (a_fields.empty()) return detail::Pack(static_cast<std::uint16_t>(kLastBit));
		std::vector<std::pair<int, std::vector<int>>> frags;
		int                                           prev = -1;
		for (const int f : a_fields) {
			if (!frags.empty() && f == prev + 1 && frags.back().second.size() < 127) frags.back().second.push_back(f);
			else {
				int skip = f - prev - 1;
				while (skip > 127) {
					frags.push_back({ 127, {} });
					skip -= 127;
				}
				frags.push_back({ skip, { f } });
			}
			prev = f;
		}
		Bytes            out;
		std::vector<int> masked;
		for (size_t i = 0; i < frags.size(); ++i) {
			const auto& [skip, values] = frags[i];
			const bool flagged = std::any_of(values.begin(), values.end(), [&](int v) { return a_zeroes.count(v) != 0; });
			if (flagged) masked.insert(masked.end(), values.begin(), values.end());
			const unsigned packed = static_cast<unsigned>(skip) | (flagged ? kZeroBit : 0) | (static_cast<unsigned>(values.size()) << kValueShift) |
			                        (i == frags.size() - 1 ? kLastBit : 0);
			detail::Append(out, detail::Pack(static_cast<std::uint16_t>(packed)));
		}
		if (!masked.empty()) {
			const size_t n = masked.size();
			Bytes        bits(n <= 8 ? 1 : n <= 16 ? 2 : 4 * ((n + 31) / 32), 0);
			for (size_t i = 0; i < n; ++i)
				if (a_zeroes.count(masked[i])) bits[i / 8] |= static_cast<std::uint8_t>(1 << (i % 8));
			detail::Append(out, bits);
		}
		return out;
	}

	// pipeline.add_field: a_export with a_value stored for struct field a_field (skipped or zero-masked before)
	inline bool AddField(Bytes& a_export, size_t a_headerPos, const std::vector<long long>& a_fieldSizes, int a_field, const Bytes& a_value, std::string& a_reason)
	{
		PropertyHeader h;
		if (!ParseHeader(a_export, a_headerPos, h, a_reason)) return false;
		const bool stored = std::find(h.fields.begin(), h.fields.end(), a_field) != h.fields.end();
		if (stored && !h.zeroed.count(a_field)) {
			a_reason = "field " + std::to_string(a_field) + " is already stored";
			return false;
		}
		std::set<int> all(h.fields.begin(), h.fields.end());
		all.insert(a_field);
		auto zeroes = h.zeroed;
		zeroes.erase(a_field);
		const auto header = BuildHeader({ all.begin(), all.end() }, zeroes);
		long long  at = static_cast<long long>(h.end);
		for (const int f : h.fields) {
			if (f >= a_field || h.zeroed.count(f)) continue;
			if (f < 0 || static_cast<size_t>(f) >= a_fieldSizes.size()) {
				a_reason = "KeyError: " + std::to_string(f);
				return false;
			}
			at += a_fieldSizes[static_cast<size_t>(f)];
		}
		Bytes out = detail::Slice(a_export, 0, static_cast<long long>(a_headerPos));
		detail::Append(out, header);
		detail::Append(out, detail::Slice(a_export, static_cast<long long>(h.end), at));
		detail::Append(out, a_value);
		detail::Append(out, detail::Slice(a_export, at, static_cast<long long>(a_export.size())));
		a_export = std::move(out);
		return true;
	}

	// pipeline.replace_export: one export's bytes swapped; its SerialSize, every later SerialOffset and BulkDataStartOffset
	// follow (offsets count from the .uasset start). Refuses anything ambiguous.
	inline bool ReplaceExport(Bytes& a_uasset, Bytes& a_uexp, long long a_serialOffset, long long a_serialSize, const Bytes& a_new, std::string& a_reason)
	{
		const long long base = static_cast<long long>(a_uasset.size());
		const long long start = a_serialOffset - base;
		if (start < 0 || start + a_serialSize > static_cast<long long>(a_uexp.size())) {
			a_reason = "export outside the .uexp";
			return false;
		}
		const long long delta = static_cast<long long>(a_new.size()) - a_serialSize;
		Bytes           uexp = detail::Slice(a_uexp, 0, start);
		detail::Append(uexp, a_new);
		detail::Append(uexp, detail::Slice(a_uexp, start + a_serialSize, static_cast<long long>(a_uexp.size())));
		Bytes           ua = a_uasset;
		const long long end = base + static_cast<long long>(a_uexp.size()) - 4;
		std::vector<long long> hits;
		for (long long q = 0; q < std::min(1024LL, base - 7); ++q) {
			long long v = 0;
			detail::ReadAt(ua, q, v);
			if (v == end) hits.push_back(q);
		}
		if (hits.size() != 1) {
			a_reason = "BulkDataStartOffset not found once (" + detail::PyList(hits) + ")";
			return false;
		}
		detail::WriteAt<long long>(ua, hits[0], end + delta);
		std::map<long long, std::vector<std::pair<long long, long long>>> rows;   // offset -> [(row position, size)]
		for (long long q = 0; q < base - 15; ++q) {
			long long size = 0, off = 0;
			detail::ReadAt(ua, q, size);
			detail::ReadAt(ua, q + 8, off);
			if (base <= off && 0 < size && size <= end - off) rows[off].emplace_back(q, size);
		}
		struct Link
		{
			long long off, q, size;
		};
		std::vector<Link> chain;
		for (long long off = base; off < end;) {
			std::vector<std::pair<long long, long long>> next;
			if (const auto it = rows.find(off); it != rows.end())
				for (const auto& [q, size] : it->second)
					if (off + size == end || rows.count(off + size)) next.emplace_back(q, size);
			if (next.size() != 1) {
				a_reason = "export table chain broken at " + std::to_string(off);
				return false;
			}
			chain.push_back({ off, next[0].first, next[0].second });
			off += next[0].second;
		}
		std::vector<Link> target;
		for (const auto& c : chain)
			if (c.off == a_serialOffset && c.size == a_serialSize) target.push_back(c);
		if (target.size() != 1) {
			a_reason = "export row not found in the export table";
			return false;
		}
		detail::WriteAt<long long>(ua, target[0].q, a_serialSize + delta);
		for (const auto& c : chain)
			if (c.off > a_serialOffset) detail::WriteAt<long long>(ua, c.q + 8, c.off + delta);
		a_uasset = std::move(ua);
		a_uexp = std::move(uexp);
		return true;
	}

	// pipeline.masked_sha256: SHA-256 with each [start, length] of a_mask zeroed (bytearray slice assignment)
	inline std::string MaskedSha256(const Bytes& a_data, const std::vector<std::pair<long long, long long>>& a_mask)
	{
		Bytes data = a_data;
		for (const auto& [start, length] : a_mask) {
			const long long n = static_cast<long long>(data.size());
			const long long from = std::clamp(start, 0LL, n), to = std::max(from, std::clamp(start + length, 0LL, n));
			data.erase(data.begin() + from, data.begin() + to);
			data.insert(data.begin() + from, static_cast<size_t>(std::max(0LL, length)), 0);
		}
		return Sha256Hex(data);
	}

	// pipeline.rewrite_package: every rewrite applied, later exports first; each export checked against its stock bytes and
	// header first
	inline bool RewritePackage(Bytes& a_uasset, Bytes& a_uexp, const std::vector<LayoutRewrite>& a_rewrites, std::string& a_reason)
	{
		std::vector<size_t> order(a_rewrites.size());
		std::iota(order.begin(), order.end(), size_t{ 0 });
		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return a_rewrites[a].exportStart > a_rewrites[b].exportStart; });
		Bytes uasset = a_uasset, uexp = a_uexp;
		for (const size_t i : order) {
			const auto& r = a_rewrites[i];
			auto        exp = detail::Slice(uexp, r.exportStart, r.exportStart + r.exportSize);
			if (MaskedSha256(exp, r.shaMask) != r.stockSha256) {
				a_reason = r.widget + ": stock export differs from the verified game build (game update?)";
				return false;
			}
			PropertyHeader h;
			if (!ParseHeader(exp, static_cast<size_t>(r.headerPos), h, a_reason)) return false;
			const std::vector<int> zeroes(h.zeroed.begin(), h.zeroed.end());
			if (h.fields != r.stockFields || zeroes != r.stockZeroes) {
				a_reason = r.widget + ": unexpected property header " + detail::PyList(h.fields) + " " + detail::PyList(zeroes);
				return false;
			}
			for (const auto& [field, value] : r.add)
				if (!AddField(exp, static_cast<size_t>(r.headerPos), r.fieldSizes, field, detail::Pack(static_cast<float>(value)), a_reason)) return false;
			for (const auto& [field, value] : r.addBytes)
				if (!AddField(exp, static_cast<size_t>(r.headerPos), r.fieldSizes, field, value, a_reason)) return false;
			if (!ReplaceExport(uasset, uexp, static_cast<long long>(uasset.size()) + r.exportStart, r.exportSize, exp, a_reason)) return false;
		}
		a_uasset = std::move(uasset);
		a_uexp = std::move(uexp);
		return true;
	}

	// pipeline.bp_float: a blueprint's layout values set from [Sizes] or fixed; an unexpected stock value refuses the package
	inline bool LayoutFloats(Bytes& a_uexp, const std::string& a_package, const LayoutRow& a_row, const Sizes& a_sizes, std::string& a_reason)
	{
		Bytes out = a_uexp;
		for (const auto& f : a_row.floats) {
			const long long width = f.isDouble ? 8 : 4;
			std::string     found = "None";
			double          value = 0;
			const bool      have = static_cast<long long>(a_uexp.size()) >= f.offset + width;
			if (have) {
				if (f.isDouble) detail::ReadAt(a_uexp, f.offset, value);
				else {
					float v = 0;
					detail::ReadAt(a_uexp, f.offset, v);
					value = v;
				}
				found = json::PyRepr(value);
			}
			if (!have || std::fabs(value - f.stock.Number()) > 1e-3) {
				a_reason = a_package + ": stock size " + found + " at byte " + std::to_string(f.offset) + " (" + f.widget + "), expected " + detail::PyNumber(f.stock) +
				           " (game update? re-scan the asset)";
				return false;
			}
			double set = f.value;
			if (f.hasSize) {
				const auto* s = a_sizes.Find(f.size);
				if (!s) {
					a_reason = "KeyError: '" + f.size + "'";
					return false;
				}
				set = *s;
			}
			if (f.isDouble) detail::WriteAt<double>(out, f.offset, set);
			else detail::WriteAt<float>(out, f.offset, static_cast<float>(set));
		}
		for (const auto& b : a_row.bytes) {
			const bool have = b.offset >= 0 && static_cast<long long>(a_uexp.size()) > b.offset;
			if (!have || static_cast<double>(a_uexp[static_cast<size_t>(b.offset)]) != b.stock.Number() || !b.stock.IsNumber()) {
				a_reason = a_package + ": stock byte " + (have ? std::to_string(a_uexp[static_cast<size_t>(b.offset)]) : std::string("None")) + " at " + std::to_string(b.offset) + " (" +
				           b.widget + "), expected " + detail::PyNumber(b.stock) + " (game update? re-scan the asset)";
				return false;
			}
			out[static_cast<size_t>(b.offset)] = static_cast<std::uint8_t>(b.value);
		}
		a_uexp = std::move(out);
		return true;
	}

	// pipeline.layout_package: the stored values in place, then the rewrites (which move later bytes)
	inline bool LayoutPackage(Bytes& a_uasset, Bytes& a_uexp, const std::string& a_package, const PackData& a_data, const Sizes& a_sizes, std::string& a_reason)
	{
		const auto it = a_data.layout.find(a_package);
		if (it == a_data.layout.end()) {
			a_reason = "KeyError: '" + a_package + "'";
			return false;
		}
		Bytes uexp = a_uexp, uasset = a_uasset;
		if (!LayoutFloats(uexp, a_package, it->second, a_sizes, a_reason)) return false;
		if (!it->second.rewrites.empty() && !RewritePackage(uasset, uexp, it->second.rewrites, a_reason)) return false;
		a_uasset = std::move(uasset);
		a_uexp = std::move(uexp);
		return true;
	}

	// pipeline.needed_names(colours=False, layout=True): the layout packages, sorted (MRLayout_P's retoc extraction)
	inline std::vector<std::string> LayoutNeededNames(const PackData& a_data)
	{
		std::set<std::string> names(a_data.layoutPackages.begin(), a_data.layoutPackages.end());
		return { names.begin(), names.end() };
	}

	// ---- the legacy package summary and swap_imports ----
	struct SummaryField
	{
		std::string name;
		long long   value = 0;
		size_t      at = 0;     // byte position ("@<field>")
		int         width = 4;  // 4 or 8
	};

	struct SummaryName
	{
		std::string   name;   // UTF-8
		std::uint16_t h1 = 0, h2 = 0;
		size_t        start = 0;
	};

	struct PackageSummary   // pipeline.package_summary (retoc to-legacy, unversioned UE 5.3)
	{
		std::vector<SummaryField> fields;
		std::string               packageName;
		std::vector<SummaryName>  names;
		size_t                    end = 0, namesEnd = 0;

		long long Get(std::string_view a_name) const
		{
			for (const auto& f : fields)
				if (f.name == a_name) return f.value;
			return 0;
		}
		const SummaryField* Find(std::string_view a_name) const
		{
			for (const auto& f : fields)
				if (f.name == a_name) return &f;
			return nullptr;
		}
	};

	// pipeline.OFFSET_FIELDS: the summary fields that move when the name map changes size
	inline constexpr std::array<const char*, 15> kOffsetFields{ "TotalHeaderSize", "SoftObjectPathsOffset", "GatherableTextDataOffset", "ExportOffset", "ImportOffset",
		"DependsOffset", "SoftPackageReferencesOffset", "SearchableNamesOffset", "ThumbnailTableOffset", "AssetRegistryDataOffset", "BulkDataStartOffset",
		"WorldTileInfoDataOffset", "PreloadDependencyOffset", "PayloadTocOffset", "DataResourceOffset" };

	namespace detail
	{
		inline void Utf8(std::string& a_out, std::uint32_t a_cp)
		{
			if (a_cp < 0x80) a_out += static_cast<char>(a_cp);
			else if (a_cp < 0x800) {
				a_out += static_cast<char>(0xC0 | (a_cp >> 6));
				a_out += static_cast<char>(0x80 | (a_cp & 0x3F));
			} else if (a_cp < 0x10000) {
				a_out += static_cast<char>(0xE0 | (a_cp >> 12));
				a_out += static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F));
				a_out += static_cast<char>(0x80 | (a_cp & 0x3F));
			} else {
				a_out += static_cast<char>(0xF0 | (a_cp >> 18));
				a_out += static_cast<char>(0x80 | ((a_cp >> 12) & 0x3F));
				a_out += static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F));
				a_out += static_cast<char>(0x80 | (a_cp & 0x3F));
			}
		}
		// pipeline._fstring: a length-prefixed string (negative length: UTF-16), without its last character, as UTF-8
		inline bool FString(const Bytes& a_data, size_t& a_pos, std::string& a_out)
		{
			std::int32_t n = 0;
			if (!ReadAt(a_data, static_cast<long long>(a_pos), n)) return false;
			a_out.clear();
			if (n < 0) {
				const long long count = -static_cast<long long>(n);
				if (static_cast<long long>(a_pos) + 4 + 2 * count > static_cast<long long>(a_data.size())) return false;
				std::vector<std::uint16_t> units(static_cast<size_t>(count));
				std::memcpy(units.data(), a_data.data() + a_pos + 4, static_cast<size_t>(2 * count));
				if (!units.empty()) units.pop_back();
				for (size_t i = 0; i < units.size(); ++i) {
					std::uint32_t cp = units[i];
					if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < units.size() && units[i + 1] >= 0xDC00 && units[i + 1] < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (units[++i] - 0xDC00);
					Utf8(a_out, cp);
				}
				a_pos += static_cast<size_t>(4 + 2 * count);
				return true;
			}
			if (a_pos + 4 + static_cast<size_t>(n) > a_data.size()) return false;
			for (std::int32_t i = 0; i + 1 < n; ++i) Utf8(a_out, a_data[a_pos + 4 + static_cast<size_t>(i)]);
			a_pos += 4 + static_cast<size_t>(n);
			return true;
		}
		inline bool Latin1(std::string_view a_utf8, Bytes& a_out)   // str.encode("latin-1"); false: a character above U+00FF
		{
			a_out.clear();
			for (size_t i = 0; i < a_utf8.size(); ++i) {
				const auto c = static_cast<std::uint8_t>(a_utf8[i]);
				if (c < 0x80) a_out.push_back(c);
				else if ((c & 0xE0) == 0xC0 && c <= 0xC3 && i + 1 < a_utf8.size()) a_out.push_back(static_cast<std::uint8_t>(((c & 0x1F) << 6) | (static_cast<std::uint8_t>(a_utf8[++i]) & 0x3F)));
				else return false;
			}
			return true;
		}
	}

	inline bool ReadPackageSummary(const Bytes& a_uasset, PackageSummary& a_out, std::string& a_reason)
	{
		a_out = {};
		std::uint32_t tag = 0;
		std::int32_t  legacy = 0, custom = 0;
		if (!detail::ReadAt(a_uasset, 0, tag) || !detail::ReadAt(a_uasset, 4, legacy)) {
			a_reason = detail::kTruncated;
			return false;
		}
		if (tag != 0x9E2A83C1 || legacy != -8) {
			a_reason = "not a UE5 legacy package summary";
			return false;
		}
		if (!detail::ReadAt(a_uasset, 24, custom)) {
			a_reason = detail::kTruncated;
			return false;
		}
		if (custom != 0) {
			a_reason = "versioned package (custom versions present)";
			return false;
		}
		size_t pos = 28;
		bool   ok = true;
		auto   take = [&](const char* a_name, char a_fmt) {   // 'i' int32, 'I' uint32, 'q' int64
			SummaryField f{ a_name, 0, pos, a_fmt == 'q' ? 8 : 4 };
			if (a_fmt == 'q') {
				long long v = 0;
				ok = ok && detail::ReadAt(a_uasset, static_cast<long long>(pos), v);
				f.value = v;
			} else if (a_fmt == 'I') {
				std::uint32_t v = 0;
				ok = ok && detail::ReadAt(a_uasset, static_cast<long long>(pos), v);
				f.value = v;
			} else {
				std::int32_t v = 0;
				ok = ok && detail::ReadAt(a_uasset, static_cast<long long>(pos), v);
				f.value = v;
			}
			pos += static_cast<size_t>(f.width);
			a_out.fields.push_back(std::move(f));
			return a_out.fields.back().value;
		};
		auto skipCount = [&](size_t a_each) {   // a count, then that many items
			std::int32_t n = 0;
			ok = ok && detail::ReadAt(a_uasset, static_cast<long long>(pos), n);
			pos += 4 + a_each * static_cast<size_t>(std::max(0, n));
		};
		auto fstring = [&](std::string* a_into) {
			std::string s;
			ok = ok && detail::FString(a_uasset, pos, s);
			if (a_into) *a_into = s;
		};
		take("TotalHeaderSize", 'i');
		fstring(&a_out.packageName);
		take("PackageFlags", 'I');
		take("NameCount", 'i');
		take("NameOffset", 'i');
		for (const char* k : { "SoftObjectPaths", "GatherableTextData", "Export", "Import" }) {
			take((std::string(k) + "Count").c_str(), 'i');
			take((std::string(k) + "Offset").c_str(), 'i');
		}
		take("DependsOffset", 'i');
		take("SoftPackageReferencesCount", 'i');
		take("SoftPackageReferencesOffset", 'i');
		take("SearchableNamesOffset", 'i');
		take("ThumbnailTableOffset", 'i');
		pos += 16;   // Guid
		skipCount(8);   // generations
		for (int i = 0; i < 2; ++i) {   // saved-by and compatible-with engine versions
			pos += 10;
			fstring(nullptr);
		}
		take("CompressionFlags", 'I');
		const auto chunks = take("CompressedChunks", 'i');
		take("PackageSource", 'I');
		const auto cook = take("AdditionalPackagesToCook", 'i');
		if (!ok) {
			a_reason = detail::kTruncated;
			return false;
		}
		if (chunks || cook) {
			a_reason = "unexpected compressed chunks or packages to cook in the summary";
			return false;
		}
		take("AssetRegistryDataOffset", 'i');
		take("BulkDataStartOffset", 'q');
		take("WorldTileInfoDataOffset", 'i');
		skipCount(4);   // chunk ids
		take("PreloadDependencyCount", 'i');
		take("PreloadDependencyOffset", 'i');
		take("NamesReferencedFromExportDataCount", 'i');
		take("PayloadTocOffset", 'q');
		take("DataResourceOffset", 'i');
		if (!ok) {
			a_reason = detail::kTruncated;
			return false;
		}
		a_out.end = pos;
		const long long nameOffset = a_out.Get("NameOffset");
		if (nameOffset != static_cast<long long>(pos)) {
			a_reason = "the name map does not follow the summary (" + std::to_string(nameOffset) + " != " + std::to_string(pos) + ")";
			return false;
		}
		const long long count = a_out.Get("NameCount");
		for (long long i = 0; i < count; ++i) {
			SummaryName n;
			n.start = pos;
			if (!detail::FString(a_uasset, pos, n.name) || !detail::ReadAt(a_uasset, static_cast<long long>(pos), n.h1) || !detail::ReadAt(a_uasset, static_cast<long long>(pos + 2), n.h2)) {
				a_reason = detail::kTruncated;
				return false;
			}
			pos += 4;
			a_out.names.push_back(std::move(n));
		}
		a_out.namesEnd = pos;
		const long long imp = a_out.Get("ImportOffset"), exp = a_out.Get("ExportOffset");
		if (imp != static_cast<long long>(pos) || exp != imp + 32 * a_out.Get("ImportCount") || a_out.Get("DependsOffset") != exp + 96 * a_out.Get("ExportCount")) {
			a_reason = "unexpected package layout (name map, 32-byte imports, 96-byte exports)";
			return false;
		}
		return true;
	}

	// pipeline.swap_imports: each from_object import (class from_class) becomes a to_class import of to_object; its outer
	// package row's name and the object's name are renamed in the name map (used by those two rows only), and every offset
	// after the name map moves by the size change. The .uexp is unchanged.
	inline bool SwapImports(Bytes& a_uasset, const std::vector<ImportSwap>& a_swaps, std::string& a_reason)
	{
		PackageSummary s;
		if (!ReadPackageSummary(a_uasset, s, a_reason)) return false;
		std::vector<std::string> names;
		for (const auto& n : s.names) names.push_back(n.name);
		Bytes           ua = a_uasset;
		const long long imp = s.Get("ImportOffset"), exp = s.Get("ExportOffset");
		const long long importCount = s.Get("ImportCount"), exportCount = s.Get("ExportCount");
		struct Row
		{
			long long    classPackage, className;
			std::int32_t outer;
			long long    objectName;
			std::int32_t extra;
		};
		std::vector<Row> rows;
		for (long long i = 0; i < importCount; ++i) {
			Row        r{};
			const auto at = imp + 32 * i;
			if (!detail::ReadAt(ua, at, r.classPackage) || !detail::ReadAt(ua, at + 8, r.className) || !detail::ReadAt(ua, at + 16, r.outer) || !detail::ReadAt(ua, at + 20, r.objectName) ||
				!detail::ReadAt(ua, at + 28, r.extra)) {
				a_reason = detail::kTruncated;
				return false;
			}
			rows.push_back(r);
		}
		std::vector<long long> exports;
		for (long long i = 0; i < exportCount; ++i) {
			long long n = 0;
			if (!detail::ReadAt(ua, exp + 96 * i + 16, n)) {
				a_reason = detail::kTruncated;
				return false;
			}
			exports.push_back(n);
		}
		auto index = [&](const std::string& a_name) -> long long {
			const auto it = std::find(names.begin(), names.end(), a_name);
			return it == names.end() ? -1 : static_cast<long long>(it - names.begin());
		};
		for (const auto& sw : a_swaps) {
			const long long pkgN = index(sw.fromPackage), objN = index(sw.fromObject), clsN = index(sw.fromClass), toCls = index(sw.toClass);
			if (pkgN < 0 || objN < 0 || clsN < 0 || toCls < 0) {
				a_reason = "import " + sw.fromObject + " not found (list.index(x): x not in list)";
				return false;
			}
			if (index(sw.toPackage) >= 0 || index(sw.toObject) >= 0) {
				a_reason = sw.toObject + " is already named in the package";
				return false;
			}
			std::vector<size_t> hits;
			for (size_t i = 0; i < rows.size(); ++i)
				if (rows[i].objectName == objN && rows[i].className == clsN) hits.push_back(i);
			if (hits.size() == 1 && rows[hits[0]].outer < 0 && static_cast<size_t>(-static_cast<long long>(rows[hits[0]].outer) - 1) >= rows.size()) {
				a_reason = "IndexError: list index out of range";
				return false;
			}
			if (hits.size() != 1 || rows[hits[0]].outer >= 0 || rows[static_cast<size_t>(-static_cast<long long>(rows[hits[0]].outer) - 1)].objectName != pkgN) {
				a_reason = "import " + sw.fromObject + " (" + sw.fromClass + ") not found once with its package";
				return false;
			}
			const size_t           row = hits[0], outer = static_cast<size_t>(-static_cast<long long>(rows[row].outer) - 1);
			std::vector<long long> users;
			for (size_t i = 0; i < rows.size(); ++i)
				for (const long long f : { rows[i].classPackage, rows[i].className, rows[i].objectName })
					if (f == pkgN || f == objN) users.push_back(static_cast<long long>(i));
			for (size_t i = 0; i < exports.size(); ++i)
				if (exports[i] == pkgN || exports[i] == objN) users.push_back(static_cast<long long>(i));
			auto                   sortedUsers = users;
			std::vector<long long> expected{ static_cast<long long>(row), static_cast<long long>(outer) };
			std::sort(sortedUsers.begin(), sortedUsers.end());
			std::sort(expected.begin(), expected.end());
			if (sortedUsers != expected) {
				a_reason = sw.fromObject + "'s names are used by other rows (" + detail::PyList(users) + ")";
				return false;
			}
			long long cp = rows[row].classPackage;   // names[i] with Python's negative indexes
			if (cp < 0) cp += static_cast<long long>(names.size());
			if (cp < 0 || cp >= static_cast<long long>(names.size())) {
				a_reason = "IndexError: list index out of range";
				return false;
			}
			if (names[static_cast<size_t>(cp)] != "/Script/Engine") {
				a_reason = sw.fromObject + " is not an Engine class import";
				return false;
			}
			rows[row].className = toCls;
			names[static_cast<size_t>(pkgN)] = sw.toPackage;
			names[static_cast<size_t>(objN)] = sw.toObject;
		}
		for (size_t i = 0; i < rows.size(); ++i) {
			const auto at = imp + 32 * static_cast<long long>(i);
			detail::WriteAt(ua, at, rows[i].classPackage);
			detail::WriteAt(ua, at + 8, rows[i].className);
			detail::WriteAt(ua, at + 16, rows[i].outer);
			detail::WriteAt(ua, at + 20, rows[i].objectName);
			detail::WriteAt(ua, at + 28, rows[i].extra);
		}
		Bytes nameMap;
		for (size_t i = 0; i < names.size(); ++i) {
			Bytes text;
			if (!detail::Latin1(names[i], text)) {
				a_reason = "UnicodeEncodeError: 'latin-1' codec can't encode a name";
				return false;
			}
			detail::Append(nameMap, detail::Pack(static_cast<std::int32_t>(text.size() + 1)));
			detail::Append(nameMap, text);
			nameMap.push_back(0);
			detail::Append(nameMap, detail::Pack(s.names[i].h1));
			detail::Append(nameMap, detail::Pack(s.names[i].h2));
		}
		const long long nameOffset = s.Get("NameOffset"), oldEnd = static_cast<long long>(s.namesEnd);
		const long long delta = static_cast<long long>(nameMap.size()) - (oldEnd - nameOffset);
		for (const char* field : kOffsetFields) {
			const auto* f = s.Find(field);
			if (!f || f->value < oldEnd) continue;
			if (f->width == 8) detail::WriteAt<long long>(ua, static_cast<long long>(f->at), f->value + delta);
			else detail::WriteAt<std::int32_t>(ua, static_cast<long long>(f->at), static_cast<std::int32_t>(f->value + delta));
		}
		for (long long i = 0; i < exportCount; ++i) {
			const auto at = exp + 96 * i + 36;
			long long  v = 0;
			if (!detail::ReadAt(ua, at, v)) {
				a_reason = detail::kTruncated;
				return false;
			}
			detail::WriteAt<long long>(ua, at, v + delta);
		}
		Bytes out = detail::Slice(ua, 0, nameOffset);
		detail::Append(out, nameMap);
		detail::Append(out, detail::Slice(ua, oldEnd, static_cast<long long>(ua.size())));
		PackageSummary check;
		if (!ReadPackageSummary(out, check, a_reason)) return false;
		if (check.Get("TotalHeaderSize") != static_cast<long long>(out.size())) {
			a_reason = "swapped header size mismatch";
			return false;
		}
		a_uasset = std::move(out);
		return true;
	}
}
