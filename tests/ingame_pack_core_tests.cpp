// In-game Pack Builder Phase 3a: the pure parts of the C++ build pipeline (src/builder/Pack*.h), a port of
// tools/pack_builder/pipeline.py (KB mods/menus-revamped/evidence/in-game-pack-builder-plan.md, Phase 3).
// - JSON read and written with Python's rules (key order, int/float, json.dumps(indent=2) text, float repr)
// - SHA-256 (the manifest hashes and the laurel stock check)
// - a legacy texture package: format, size and every mip (inline and .ubulk) from the data resource table; writing back
//   reproduces UE4-DDS-Tools' package changes (PackageSource "MOD ", OuterIndex 1, Size64Bit) and refuses a size change
// - the 3a recipes give the Python recipes' bytes on synthetic blocks of every branch (tests/ingame_pack_core_expect.inc,
//   written by tests/ingame_pack_core_expect.py from the Python functions)
// - package literal patches check the stock value first and never half-write
// - the data files and the colour values are read like load_menus / pack_values; the manifest has the Python key order
// Byte parity on the real game assets: tests/ingame_pack_parity_tests.py.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../src/builder/PackBuild.h"
#include "../src/builder/PackJson.h"
#include "../src/builder/PackSha256.h"

using namespace builder::pack;
namespace model = builder::model;

unsigned checks = 0, failures = 0;
void Check(bool ok, const char* why)
{
	++checks;
	if (!ok) {
		++failures;
		std::printf("FAIL %s\n", why);
	}
}

static Bytes Hex(const std::string& h)
{
	Bytes out;
	for (size_t i = 0; i + 1 < h.size(); i += 2) out.push_back(static_cast<std::uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16)));
	return out;
}

struct Expect
{
	const char* name;
	const char* hex;
};

static Bytes Expected(const Expect* table, size_t n, const char* name)
{
	for (size_t i = 0; i < n; ++i)
		if (std::strcmp(table[i].name, name) == 0) return Hex(table[i].hex);
	return {};
}

// ---- a synthetic legacy texture package (the summary field order of UE4-DDS-Tools UassetFileSummary, UE5.3) ----
template <class T> static void Put(Bytes& b, T v)
{
	const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
	b.insert(b.end(), p, p + sizeof(T));
}
template <class T> static T Get(const Bytes& b, size_t at)
{
	T v{};
	if (at + sizeof(T) > b.size()) return v;   // a short buffer reads as zero (the stub run must count, not crash)
	std::memcpy(&v, b.data() + at, sizeof(T));
	return v;
}

struct Synthetic
{
	Bytes  uasset, uexp, ubulk;
	size_t sourceAt = 0, tableAt = 0;
};

// mips: {size, inBulk}; the uexp holds a header, the FString PF_<format>, then the inline mips back to back
static Synthetic MakeTexture(const char* format, int w, int h, const std::vector<std::pair<int, bool>>& mips, std::uint32_t inlineFlags = 0x48)
{
	Synthetic s;
	Bytes&    a = s.uasset;
	Put<std::uint32_t>(a, 0x9E2A83C1);
	Put<std::int32_t>(a, -8);
	for (int i = 0; i < 5; ++i) Put<std::int32_t>(a, 0);   // version info, no custom versions
	const size_t totalAt = a.size();
	Put<std::int32_t>(a, 0);
	Put<std::int32_t>(a, 6);
	for (char c : std::string("/Game")) a.push_back(static_cast<std::uint8_t>(c));
	a.push_back(0);
	Put<std::uint32_t>(a, 0x80002000);
	for (int i = 0; i < 2 + 2 + 2 + 4 + 1 + 3 + 1; ++i) Put<std::int32_t>(a, 0);   // names..thumbnail
	for (int i = 0; i < 16; ++i) a.push_back(0);                                   // guid
	Put<std::int32_t>(a, 1);
	Put<std::int32_t>(a, 1);
	Put<std::int32_t>(a, 9);   // one generation
	for (int i = 0; i < 28 + 8; ++i) a.push_back(0);
	s.sourceAt = a.size();
	Put<std::uint32_t>(a, 0x11223344);
	for (int i = 0; i < 1 + 2 + 1 + 2 + 2 + 1; ++i) Put<std::int32_t>(a, 0);   // additional..referenced names
	Put<std::int64_t>(a, 0);                                                   // payload toc
	const size_t droAt = a.size();
	Put<std::int32_t>(a, 0);
	for (int i = 0; i < 24; ++i) a.push_back(0xAB);   // name map, exports... (not read)
	s.tableAt = a.size();
	std::memcpy(a.data() + droAt, &s.tableAt, 4);
	Put<std::int32_t>(a, 1);
	Put<std::int32_t>(a, static_cast<std::int32_t>(mips.size()));
	Bytes& x = s.uexp;
	for (int i = 0; i < 40; ++i) x.push_back(0x5A);
	Put<std::int32_t>(x, w);
	Put<std::int32_t>(x, h);
	Put<std::int32_t>(x, 0);
	const std::string pf = std::string("PF_") + format;
	Put<std::int32_t>(x, static_cast<std::int32_t>(pf.size() + 1));
	for (char c : pf) x.push_back(static_cast<std::uint8_t>(c));
	x.push_back(0);
	int seed = 1, level = 0;
	for (const auto& [size, bulk] : mips) {
		const int mw = std::max(1, w >> level), mh = std::max(1, h >> level);   // the mip record UE writes: the real mip size
		++level;
		Bytes&       to = bulk ? s.ubulk : x;
		const auto   at = static_cast<std::int64_t>(to.size());
		for (int i = 0; i < size; ++i) to.push_back(static_cast<std::uint8_t>(seed++ * 37));
		if (!bulk) {
			Put<std::int32_t>(x, mw);
			Put<std::int32_t>(x, mh);
			Put<std::int32_t>(x, 1);
		}
		Put<std::uint32_t>(a, 0);
		Put<std::int64_t>(a, at);
		Put<std::int64_t>(a, -1);
		Put<std::int64_t>(a, size);
		Put<std::int64_t>(a, size);
		Put<std::int32_t>(a, 0);
		Put<std::uint32_t>(a, bulk ? 0x10501u : inlineFlags);
	}
	for (int i = 0; i < 12; ++i) x.push_back(0);
	Put<std::uint32_t>(x, 0x9E2A83C1);
	const auto total = static_cast<std::int32_t>(a.size());
	std::memcpy(a.data() + totalAt, &total, 4);
	return s;
}

static model::Value V(std::initializer_list<double> l)
{
	model::Value v;
	for (double d : l) v.v[static_cast<size_t>(v.n++)] = d;
	return v;
}

int main()
{
	std::string error;

	// ---- JSON (json.loads / json.dumps(indent=2)) ----
	{
		json::Value v;
		Check(json::Parse(R"({"b": [1, 2.0, -3e2], "a": {"x": "é😀\n\"q\""}, "t": true, "n": null})", v, error), "parses an object");
		Check(v.type == json::Type::Object && v.keys.size() == 4 && v.keys[0] == "b" && v.keys[1] == "a", "object keys keep their file order");
		const auto* b = v.Find("b");
		Check(b && b->items.size() == 3 && b->items[0].type == json::Type::Int && b->items[1].type == json::Type::Float &&
				b->items[2].type == json::Type::Float && b->items[2].f == -300.0,
			"numbers stay int or float as written");
		const auto* x = v.Find("a") ? v.Find("a")->Find("x") : nullptr;
		Check(x && x->s == "\xC3\xA9\xF0\x9F\x98\x80\n\"q\"", "string escapes, \\u and surrogate pairs to UTF-8");
		Check(v.Find("t") && v.Find("t")->b && v.Find("n") && v.Find("n")->type == json::Type::Null, "true and null");
		Check(!json::Parse("{\"a\": [1, 2,]}", v, error) && !error.empty(), "a trailing comma is refused");
		Check(!json::Parse("{\"a\": 1} x", v, error), "text after the value is refused");

		Check(json::PyRepr(24.0) == "24.0" && json::PyRepr(0.01) == "0.01" && json::PyRepr(-0.0) == "-0.0", "repr: whole floats keep .0");
		Check(json::PyRepr(1e-05) == "1e-05" && json::PyRepr(1e16) == "1e+16" && json::PyRepr(0.1 + 0.2) == "0.30000000000000004",
			"repr: exponent form and shortest digits like Python");
		Check(json::PyRepr(123456789012345.0) == "123456789012345.0" && json::PyRepr(0.0001) == "0.0001", "repr: fixed form range");

		json::Value o = json::MakeObject();
		json::Value list = json::MakeArray();
		list.items.push_back(json::MakeFloat(1.0));
		json::Value two;
		two.type = json::Type::Int;
		two.i = 2;
		list.items.push_back(two);
		json::Set(o, "a", list);
		json::Set(o, "b", json::MakeObject());
		json::Set(o, "c", json::MakeArray());
		json::Set(o, "d", json::MakeString("\xC3\xA9\"\n\x01"));
		json::Set(o, "e", json::MakeBool(true));
		json::Set(o, "f", json::Value{});
		Check(json::Dump(o) == "{\n  \"a\": [\n    1.0,\n    2\n  ],\n  \"b\": {},\n  \"c\": [],\n  \"d\": \"\\u00e9\\\"\\n\\u0001\",\n  \"e\": true,\n  \"f\": null\n}",
			"Dump prints what json.dumps(indent=2) prints");
		json::Set(o, "a", json::MakeBool(false));
		Check(o.keys.size() == 6 && o.keys[0] == "a" && o.items[0].type == json::Type::Bool, "Set on an existing key keeps its place");
	}

	// ---- SHA-256 ----
	Check(Sha256Hex(Bytes{}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 of nothing");
	{
		const std::string abc = "abc", two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
		Check(Sha256Hex(reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size()) ==
				"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 abc");
		Check(Sha256Hex(reinterpret_cast<const std::uint8_t*>(two.data()), two.size()) ==
				"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "sha256 two blocks");
		const std::string million(1000000, 'a');
		Check(Sha256Hex(reinterpret_cast<const std::uint8_t*>(million.data()), million.size()) ==
				"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "sha256 one million a");
	}

	// ---- legacy texture packages ----
	{
		auto           s = MakeTexture("DXT5", 8, 4, { { 32, true }, { 16, false }, { 16, false } });
		TexturePackage t;
		Check(ParseTexture(s.uasset, s.uexp, &s.ubulk, t, error), "parses a texture package");
		Check(t.format == PixelFormat::DXT5 && t.width == 8 && t.height == 4 && t.mips.size() == 3, "format, size and mip count");
		Check(t.mips.size() == 3 && t.mips[0].inBulk && !t.mips[1].inBulk && t.mips[0].size == 32 && t.packageSourceAt == s.sourceAt,
			"mip places and the PackageSource field");
		const Bytes body = GatherMips(t, s.uexp, &s.ubulk);
		Bytes       want(s.ubulk.begin(), s.ubulk.begin() + 32);
		for (const auto& m : t.mips)
			if (!m.inBulk) want.insert(want.end(), s.uexp.begin() + m.offset, s.uexp.begin() + m.offset + m.size);
		Check(body == want && body.size() == 64, "the body is every mip in table order");

		Bytes recoloured(body.size());
		for (size_t i = 0; i < recoloured.size(); ++i) recoloured[i] = static_cast<std::uint8_t>(i);
		Bytes ua = s.uasset, ux = s.uexp, ub = s.ubulk;
		Check(InjectMips(t, recoloured, ua, ux, &ub, error), "writes a same-size body back");
		TexturePackage again;
		Check(ParseTexture(ua, ux, &ub, again, error) && GatherMips(again, ux, &ub) == recoloured, "the written mips read back");
		Check(Get<std::uint32_t>(ua, s.sourceAt) == 0x20444F4Du, "PackageSource becomes MOD (update_package_source(is_official=False))");
		bool entries = true;
		for (size_t i = 0; i < 3; ++i) {
			const size_t at = s.tableAt + 8 + 44 * i;
			const auto   flags = Get<std::uint32_t>(ua, at + 40), was = Get<std::uint32_t>(s.uasset, at + 40);
			entries = entries && Get<std::int32_t>(ua, at + 36) == 1 && flags == (was | 0x2000u);
		}
		Check(entries, "every data resource: OuterIndex 1, LegacyBulkDataFlags | Size64Bit");
		size_t changed = 0;
		for (size_t i = 0; i < ua.size() && i < s.uasset.size(); ++i) changed += ua[i] != s.uasset[i];
		Check(ua.size() == s.uasset.size() && changed == 4 + 3 * 2, "nothing else in the .uasset changes");
		Check(ux.size() == s.uexp.size() && Get<std::uint32_t>(ux, ux.size() - 4) == 0x9E2A83C1, "the .uexp keeps its size and tail");
		// mips 1 and 2 (4x2 and 2x1, inline): UE4-DDS-Tools writes the block-padded DDS mip size into each mip record
		const size_t rec1 = static_cast<size_t>(t.mips[1].offset + t.mips[1].size), rec2 = static_cast<size_t>(t.mips[2].offset + t.mips[2].size);
		Check(Get<std::int32_t>(s.uexp, rec1) == 4 && Get<std::int32_t>(s.uexp, rec1 + 4) == 2 && Get<std::int32_t>(s.uexp, rec2) == 2, "stock mip records hold the real sizes");
		Check(Get<std::int32_t>(ux, rec1) == 4 && Get<std::int32_t>(ux, rec1 + 4) == 4 && Get<std::int32_t>(ux, rec1 + 8) == 1 &&
				Get<std::int32_t>(ux, rec2) == 4 && Get<std::int32_t>(ux, rec2 + 4) == 4,
			"a compressed mip smaller than a block is written as 4x4 (max(4, size // 2), padded to 4)");


		Bytes ua2 = s.uasset, ux2 = s.uexp, ub2 = s.ubulk;
		Check(!InjectMips(t, Bytes(63), ua2, ux2, &ub2, error) && ua2 == s.uasset && ux2 == s.uexp && ub2 == s.ubulk,
			"a body of another size is refused and nothing is written");
		Check(!ParseTexture(s.uasset, s.uexp, nullptr, t, error), "a .ubulk mip without the .ubulk file is refused");
		Bytes bad = s.uasset;
		bad[0] = 0;
		Check(!ParseTexture(bad, s.uexp, &s.ubulk, t, error), "a wrong package tag is refused");
		Bytes shortUexp(s.uexp.begin(), s.uexp.begin() + 70);
		Check(!ParseTexture(s.uasset, shortUexp, &s.ubulk, t, error), "a mip past the end of the .uexp is refused");

		auto           b = MakeTexture("B8G8R8A8", 2, 1, { { 8, false } });
		TexturePackage tb;
		Check(ParseTexture(b.uasset, b.uexp, nullptr, tb, error) && tb.format == PixelFormat::B8G8R8A8, "B8G8R8A8");
		auto           b2 = MakeTexture("B8G8R8A8", 2, 2, { { 16, false }, { 4, false } });
		TexturePackage tb2;
		Bytes          b2a = b2.uasset, b2x = b2.uexp;
		Check(ParseTexture(b2.uasset, b2.uexp, nullptr, tb2, error) && InjectMips(tb2, Bytes(20, 7), b2a, b2x, nullptr, error) &&
				Get<std::int32_t>(b2x, static_cast<size_t>(tb2.mips[1].offset + 4)) == 1,
			"an uncompressed 1x1 mip keeps its record (block size 1)");
		auto           g = MakeTexture("G8", 4, 4, { { 16, false } });
		TexturePackage tg;
		Check(ParseTexture(g.uasset, g.uexp, nullptr, tg, error) && tg.format == PixelFormat::G8, "G8");
		auto           f = MakeTexture("FloatRGBA", 1, 1, { { 8, false } });
		TexturePackage tf;
		Check(ParseTexture(f.uasset, f.uexp, nullptr, tf, error) && tf.format == PixelFormat::Unknown, "another format parses as Unknown");
	}

	// ---- the 3a recipes against the Python recipes (synthetic blocks of every branch) ----
	{
#include "ingame_pack_core_expect.inc"
		const size_t n = sizeof(table) / sizeof(table[0]);
		PackData     data;
		auto run = [&](const char* recipe, PixelFormat fmt, const std::string& hex, const Values& v) {
			Dds d;
			d.format = fmt;
			d.body = Hex(hex);
			Bytes out;
			std::string skip;
			return BakeTexture(recipe, d, v, out, skip) ? out : Bytes{};
		};
		Values v;
		v["rowIcon"] = V({ 66, 99, 153 });
		Check(run("flat:rowIcon", PixelFormat::DXT5, bc3, v) == Expected(table, n, "flat_bc3"), "flat on BC3");
		Check(run("flat:rowIcon", PixelFormat::B8G8R8A8, bgra, v) == Expected(table, n, "flat_bgra"), "flat on B8G8R8A8");
		v["rowIcon"] = V({ 2.5, 127.5, 0.5 });
		Check(run("flat:rowIcon", PixelFormat::DXT5, bc3, v) == Expected(table, n, "flat_half"), "flat rounds half to even like round()");
		Check(run("clear", PixelFormat::DXT5, bc3, {}) == Expected(table, n, "clear"), "clear");
		v["pattern"] = V({ 66, 99, 153 });
		v["Skin.pattern"] = V({ 1, 1, 1, 0.6 });
		Check(run("laurels", PixelFormat::DXT5, bc3, v) == Expected(table, n, "laurels_alpha"), "laurels with opacity");
		v["Skin.pattern"] = V({ 0.5, 0.7, 1, 1 });
		Check(run("laurels", PixelFormat::DXT5, bc3, v) == Expected(table, n, "laurels_tint"), "laurels with a tint");
		v["frame"] = V({ 16, 16, 16 });
		v["panel"] = V({ 41, 41, 41 });
		Check(run("panel", PixelFormat::DXT5, bc3, v) == Expected(table, n, "panel"), "panel (endpoint curve)");
		v["enchantingFrame"] = V({ 67, 75, 91 });
		Check(run("altar_metal:enchantingFrame", PixelFormat::DXT5, bc3, v) == Expected(table, n, "altar_bc3"), "altar_metal BC3");
		Check(run("altar_metal:enchantingFrame", PixelFormat::DXT1, bc1, v) == Expected(table, n, "altar_bc1"), "altar_metal BC1 (modes kept)");
		Check(run("altar_metal:enchantingFrame", PixelFormat::B8G8R8A8, bgra, v) == Expected(table, n, "altar_bgra"), "altar_metal B8G8R8A8");
		Check(recipes::MapColours(PixelFormat::DXT1, Hex(bc1), [](const std::array<double, 3>& c) {
				  return std::array<double, 3>{ 255 - c[0], 255 - c[1], 255 - c[2] };
			  }) == Expected(table, n, "invert_bc1"),
			"map_colours flips BC1 endpoints and indices when the order turns");
		v["filterMark"] = V({ 92, 140, 214 });
		Check(run("tone:filterMark", PixelFormat::DXT5, bc3, v) == Expected(table, n, "tone"), "tone");
		v["listHighlight"] = V({ 91, 91, 91 });
		Check(run("neutral:listHighlight:59.75", PixelFormat::DXT5, bc3, v) == Expected(table, n, "neutral"), "neutral");
		Check(run("grey_scale:1.2", PixelFormat::DXT5, bc3, {}) == Expected(table, n, "grey_bc3"), "grey_scale BC3");
		Check(run("grey_scale:0.45", PixelFormat::DXT1, bc1, {}) == Expected(table, n, "grey_bc1"), "grey_scale BC1");
		Check(run("grey_scale:2.2", PixelFormat::B8G8R8A8, bgra, {}) == Expected(table, n, "grey_bgra"), "grey_scale B8G8R8A8");
		v["Skin.ornament"] = V({ 0.45, 0.45, 0.45, 1 });
		Check(run("skin_flat:ornament:235", PixelFormat::DXT5, bc3, v) == Expected(table, n, "skin_flat"), "skin_flat");

		auto keys = [&](const char* name) { return Recipe(name, data).keys; };
		Check(Recipe("flat:rowIcon", data).kind == RecipeKind::Texture && keys("flat:rowIcon") == std::vector<std::string>{ "rowIcon" }, "flat reads its key");
		Check(keys("laurels") == std::vector<std::string>{ "pattern", "Skin.pattern" } && keys("panel") == std::vector<std::string>{ "frame", "panel" },
			"recipe keys in pipeline.recipe() order");
		Check(keys("skin_flat:ornament:235") == std::vector<std::string>{ "Skin.ornament" } && keys("clear").empty() && keys("flat_white").empty(),
			"skin_flat, clear, flat_white keys");
		Check(Recipe("parchment", data).kind == RecipeKind::Later && Recipe("charcoal:inventory", data).kind == RecipeKind::Later &&
				Recipe("line_tint:1.6", data).kind == RecipeKind::Later,
			"image recipes wait for their phase");
		Check(Recipe("nonsense", data).kind == RecipeKind::Unknown, "an unknown recipe");
		Check(FormatSupported("flat", PixelFormat::DXT5) && FormatSupported("flat", PixelFormat::B8G8R8A8) && !FormatSupported("flat", PixelFormat::DXT1),
			"flat: BC3 or 32-bit");
		Check(FormatSupported("grey_scale", PixelFormat::DXT1) && FormatSupported("altar_metal", PixelFormat::B8G8R8A8) &&
				!FormatSupported("tone", PixelFormat::DXT1) && FormatSupported("paper_scale", PixelFormat::G8) && !FormatSupported("flat", PixelFormat::G8) &&
				FormatSupported("skill_icon", PixelFormat::DXT1) && !FormatSupported("skill_icon", PixelFormat::DXT5) &&
				FormatSupported("quest_icon", PixelFormat::B8G8R8A8) && !FormatSupported("quest_icon", PixelFormat::DXT1),
			"format_supported per kind");
	}

	// ---- the data files (load_menus, load_literals) ----
	PackData data;
	{
		const std::vector<std::pair<std::string, std::string>> files = {
			{ "b.json", R"({"menu": "B", "order": 2, "assets": [{"asset": "Art/X/T_B", "recipe": "flat:rowIcon"}],
				"blueprint_colours": {"WBP_A": [{"offset": 20, "stock": [0.5, 0.25, 0.125, 1.0], "colour": "rowText"},
				                                {"offset": 36, "stock": [1, 1, 1, 0.5], "colour": "white"},
				                                {"offset": 52, "stock": [0.0, 0.0, 0.0, 1.0], "colour": "pattern", "scale": 1.6}]},
				"blueprint_literals": {"WBP_A": [{"offset": 68, "type": "double", "stock": 1.0, "value": 0.0, "widget": "Shadow"},
				                                 {"offset": 76, "type": "int32", "stock": 3, "value": 5, "widget": "Slot"},
				                                 {"offset": 80, "stock": 2.5, "value": 3.25, "widget": "Size"}]},
				"import_swaps": {"WBP_Popup": []},
				"rich_rows": {"DT_UI_MagicMenu": {"Gold": {"offset": 4, "stock": [1.0, 0.5, 0.0, 1.0]}}}})" },
			{ "a.json", R"({"menu": "A", "order": 1, "assets": [{"asset": "Art/X/T_A", "recipe": "clear"}, {"asset": "Art/X/T_C", "recipe": "bp_colour:WBP_A"}],
				"glyph_masks": {"tab": ["T_Mask1", "T_Mask0"]},
				"material_colours": {"M_Scroll": [{"offset": 8, "stock": [0.2, 0.2, 0.2, 1.0], "colour": "Skin.scrollTrack"}]},
				"layout": {"WBP_Layout": {"asset": "UI/WBP_Layout"}}})" },
		};
		Check(LoadPackData(files, data, error), "reads the data files");
		Check(data.textures.size() == 3 && data.textures[0].first == "Art/X/T_A" && data.textures[1].first == "Art/X/T_C" &&
				data.textures[2].first == "Art/X/T_B",
			"assets in the data files' order (order, then menu)");
		Check(data.blueprints.count("WBP_A") && data.blueprints["WBP_A"].size() == 3 && data.blueprints["WBP_A"][2].hasScale &&
				data.blueprints["WBP_A"][2].scale == 1.6 && data.blueprints.count("M_Scroll"),
			"blueprint and material colours, with their scale");
		Check(data.literals["WBP_A"].size() == 3 && data.literals["WBP_A"][1].type == "int32" && data.literals["WBP_A"][2].type.empty(),
			"blueprint literals");
		Check(data.importSwaps.count("WBP_Popup") && data.glyphMasks["tab"].size() == 2 && data.richRows["DT_UI_MagicMenu"].count("Gold") &&
				data.layoutPackages == std::vector<std::string>{ "WBP_Layout" },
			"import swaps, glyph masks, rich rows, layout packages");
		Check(NeededNames(data, false) == std::vector<std::string>{ "T_A", "T_B", "T_C", "T_Enchantment_Masks", "T_Mask0", "T_Mask1" },
			"needed names: assets, glyph masks and the panel mask, sorted");
		Check(NeededNames(data, true).size() == 7, "needed names with the layout packages");
		Check(Recipe("bp_colour:WBP_A", data).kind == RecipeKind::Package && Recipe("bp_colour:WBP_A", data).keys == std::vector<std::string>{ "pattern", "rowText" },
			"bp_colour keys: its colour names sorted, constants left out");
		Check(Recipe("material_colour:M_Scroll", data).keys == std::vector<std::string>{ "Skin.scrollTrack" }, "material_colour keys");
		Check(Recipe("rich_row:Gold:effectValue", data).kind == RecipeKind::Package && Recipe("rich_row:Gold:effectValue", data).keys == std::vector<std::string>{ "effectValue" },
			"rich_row keys");
		PackData dup;
		Check(!LoadPackData({ files[1], { "c.json", R"({"assets": [{"asset": "Art/X/T_A", "recipe": "clear"}]})" } }, dup, error), "an asset listed twice is refused");
		Check(!LoadPackData({ { "c.json", "{not json" } }, dup, error) && error.find("c.json") != std::string::npos, "broken JSON names the file");
		Check(AssetName("Art/X/T_A") == "T_A" && AssetName("T_B") == "T_B", "AssetName");
	}

	// ---- package literal patches ----
	{
		Bytes uexp(96, 0);
		auto  put4 = [&](size_t at, float a, float b, float c, float d) {
            const float f[4] = { a, b, c, d };
            std::memcpy(uexp.data() + at, f, 16);
		};
		put4(20, 0.5f, 0.25f, 0.125f, 1.0f);
		put4(36, 1, 1, 1, 0.5f);
		put4(52, 0, 0, 0, 1);
		const double one = 1.0;
		std::memcpy(uexp.data() + 68, &one, 8);
		const std::int32_t three = 3;
		std::memcpy(uexp.data() + 76, &three, 4);
		const float twoHalf = 2.5f;
		std::memcpy(uexp.data() + 80, &twoHalf, 4);
		Values v;
		v["rowText"] = V({ 239, 239, 239 });
		v["pattern"] = V({ 66, 99, 153 });
		Bytes       out;
		std::string skip;
		Check(BakePackage("bp_colour:WBP_A", "WBP_A", uexp, v, data, out, skip), "bp_colour patches a blueprint");
		const float lin239 = static_cast<float>(model::Linear(239));
		Check(out.size() == uexp.size() && Get<float>(out, 20) == lin239 && Get<float>(out, 32) == 1.0f, "an sRGB colour as linear floats, stock alpha kept");
		Check(Get<float>(out, 36) == 1.0f && Get<float>(out, 48) == 0.5f, "a constant colour (white), alpha 0.5 kept");
		Check(Get<float>(out, 52) == static_cast<float>(model::Linear(std::min(255.0, 66.0 * 1.6))) &&
				Get<float>(out, 60) == static_cast<float>(model::Linear(std::min(255.0, 153.0 * 1.6))),
			"a scaled colour (x 1.6, at most 255)");
		Check(Get<double>(out, 68) == 0.0 && Get<std::int32_t>(out, 76) == 5 && Get<float>(out, 80) == 3.25f, "double, int32 and float literals");
		Bytes moved = uexp;
		put4(36, 0.9f, 1, 1, 0.5f);
		moved = uexp;
		out.clear();
		Check(!BakePackage("bp_colour:WBP_A", "WBP_A", moved, v, data, out, skip) && !skip.empty() && out.empty(),
			"an unexpected stock colour skips the whole package");
		put4(36, 1, 1, 1, 0.5f);
		Bytes lit = uexp;
		const std::int32_t four = 4;
		std::memcpy(lit.data() + 76, &four, 4);
		Check(!BakePackage("bp_colour:WBP_A", "WBP_A", lit, v, data, out, skip), "an unexpected stock literal skips the package");
		Check(!BakePackage("bp_colour:WBP_A", "WBP_A", Bytes(30, 0), v, data, out, skip), "a short .uexp skips the package");

		Bytes table(24, 0);
		const float gold[4] = { 1.0f, 0.5f, 0.0f, 1.0f };
		std::memcpy(table.data() + 4, gold, 16);
		v["effectValue"] = V({ 185, 138, 69 });
		Check(BakePackage("rich_row:Gold:effectValue", "DT_UI_MagicMenu", table, v, data, out, skip) && Get<float>(out, 4) == static_cast<float>(model::Linear(185)) &&
				Get<float>(out, 16) == 1.0f,
			"rich_row sets the row colour (alpha 1)");
	}

	// ---- the colour values (custom_colours.pack_values) ----
	{
		Values v;
		Check(PackValues(nullptr, v, error), "defaults without a file");
		Check(v.count("frame") && v["frame"].n == 3 && v.count("detailsPage") && v["detailsPage"].n == 1 && v.count("selected") &&
				v.count("Skin.pattern") && v["Skin.pattern"].n == 4 && v.count("Skin.scrollTrack") && v["Skin.scrollTrack"].n == 1 && !v.count("Skin.band") &&
				!v.count("Skin.rule"),
			"[Colours], [Tab] by name, only the baked [Skin] tints");
		const std::string file = "[Colours]\nframe = 10, 20, 30\n[Tab]\nselected = 50\n[Skin]\npattern = 0.5, 0.5, 0.5, 1\nrule = 0.1, 0.1, 0.1, 1\n[Roles]\nframe = 1, 2, 3\n";
		Check(PackValues(&file, v, error), "a colour file");
		Check(v["frame"].v[0] == 10 && v["frame"].v[2] == 30 && v["selected"].v[0] == 50 && v["Skin.pattern"].v[0] == 0.5, "the file's values win");
		const std::string broken = "[Colours]\nframe = 10, 20\n";
		Check(!PackValues(&broken, v, error) && !error.empty(), "a broken value refuses the file");
	}

	// ---- one asset baked (pipeline.bake) and the manifest ----
	{
		auto         s = MakeTexture("DXT5", 4, 4, { { 16, false } });
		PackageFiles stock{ s.uasset, s.uexp, {}, false };
		Values       v;
		v["rowIcon"] = V({ 66, 99, 153 });
		BakedAsset  baked;
		std::string reason;
		Check(BakeAsset("Art/X/T_B", "flat:rowIcon", stock, v, data, baked, reason) == BakeResult::Baked, "bakes a texture asset");
		TexturePackage t;
		Check(ParseTexture(baked.files.uasset, baked.files.uexp, nullptr, t, error) && Get<std::uint32_t>(baked.files.uasset, s.sourceAt) == 0x20444F4Du,
			"the staged package is the injected one");
		Check(baked.keys == std::vector<std::string>{ "rowIcon" }, "the values it read");
		Check(json::Dump(baked.entry) == "{\n  \"recipe\": \"flat:rowIcon\",\n  \"style\": [\n    \"rowIcon\"\n  ],\n  \"stock_uexp_sha256\": \"" + Sha256Hex(s.uexp) +
					"\",\n  \"baked_uexp_sha256\": \"" + Sha256Hex(baked.files.uexp) + "\"\n}",
			"the manifest entry (recipe, style, stock and baked .uexp hashes)");
		v["pattern"] = V({ 66, 99, 153 });
		v["Skin.pattern"] = V({ 1, 1, 1, 1 });
		Check(BakeAsset("Art/X/T_L", "laurels", stock, v, data, baked, reason) == BakeResult::Skipped &&
				reason == "laurel stock asset differs from the verified game build",
			"laurels refuses another stock asset");
		auto bc1 = MakeTexture("DXT1", 4, 4, { { 8, false } });
		Check(BakeAsset("Art/X/T_B", "flat:rowIcon", { bc1.uasset, bc1.uexp, {}, false }, v, data, baked, reason) == BakeResult::Skipped &&
				reason == "the stock texture is not BC3 (game update?)",
			"an unsupported format is skipped");
		Check(BakeAsset("Art/X/T_P", "parchment", stock, v, data, baked, reason) == BakeResult::Later, "a later recipe is not baked yet");
		Check(BakeAsset("Art/X/T_P", "nonsense", stock, v, data, baked, reason) == BakeResult::Skipped && reason == "KeyError: 'nonsense'",
			"an unknown recipe is skipped like the KeyError");
		Check(BakeAsset("Art/X/T_B", "flat:missing", stock, v, data, baked, reason) == BakeResult::Skipped && reason == "KeyError: 'missing'",
			"a missing value is skipped like the KeyError");

		BuildRecord record;
		Check(BakeAsset("Art/X/T_B", "flat:rowIcon", stock, v, data, baked, reason) == BakeResult::Baked, "bakes again");
		record.Add("Art/X/T_B", baked, v);
		record.Skip("Art/X/T_L", "laurels", "laurel stock asset differs from the verified game build");
		const auto manifest = json::Dump(Manifest("MRCustom_P", "MenusRevamped.dll", record, { { "MRCustom_P.pak", "aa" }, { "MRCustom_P.utoc", "bb" } }));
		const std::string head = "{\n  \"pack\": \"MRCustom_P\",\n  \"game_build\": \"1.512.105.0\",\n  \"built_by\": \"MenusRevamped.dll\",\n"
								 "  \"builder_version\": \"0.5.0\",\n  \"style\": {\n    \"rowIcon\": [\n      66.0,\n      99.0,\n      153.0\n    ]\n  },\n"
								 "  \"textures\": {\n    \"Art/X/T_B\": {\n      \"recipe\": \"flat:rowIcon\",";
		Check(manifest.starts_with(head), "manifest head: the Python key order, style values as floats");
		Check(manifest.ends_with("  \"skipped\": [\n    {\n      \"asset\": \"Art/X/T_L\",\n      \"recipe\": \"laurels\",\n      \"reason\": \"laurel stock asset differs from the verified game build\"\n    }\n  ],\n"
								 "  \"files\": {\n    \"MRCustom_P.pak\": \"aa\",\n    \"MRCustom_P.utoc\": \"bb\"\n  }\n}"),
			"manifest tail: skipped, then the file hashes");
	}

	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
