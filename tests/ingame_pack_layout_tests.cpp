// In-game Pack Builder Phase 3d: the size/layout half of the C++ build pipeline (src/builder/PackLayout.h, PackBuild.h,
// PackData.h), a port of tools/pack_builder/pipeline.py's package rewrite (KB techniques/unversioned-package-rewrite.md).
// The oracle is tests/ingame_pack_layout_expect.inc, written by tests/ingame_pack_layout_expect.py from the Python functions:
// - unversioned property headers: parse and build (fragments, zero mask) on fixed and random field sets
// - add_field (a skipped field, a zero-masked field, the refusal), replace_export (sizes, later offsets, BulkDataStartOffset,
//   every refusal), rewrite_package (later exports first, the stock SHA-256 with its mask, the header check)
// - package_summary and swap_imports on a synthetic legacy package (rename, offsets after the name map, every refusal)
// - bp_float (floats, doubles, bytes, the stock check and its message), layout_package (transactional)
// - [Sizes] like custom_colours.load_sizes and the manifest's "sizes"; write_manifest's header keys
// - the data files' layout and import_swaps sections; layout_entries and the layout-skipped row
// Byte parity on the real game assets: tests/ingame_pack_parity_tests.py.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "../src/builder/PackBuild.h"
#include "../src/builder/PackLayout.h"

using namespace builder::pack;
namespace model = builder::model;

#include "ingame_pack_layout_expect.inc"

unsigned checks = 0, failures = 0;
void Check(bool ok, const std::string& why)
{
	++checks;
	if (!ok) {
		++failures;
		std::printf("FAIL %s\n", why.c_str());
	}
}

static std::map<std::string, std::string> expect;
static const std::string& E(const std::string& k)
{
	static const std::string none;
	const auto it = expect.find(k);
	if (it == expect.end()) {
		Check(false, "no expectation " + k);
		return none;
	}
	return it->second;
}

static Bytes Hex(const std::string& h)
{
	Bytes out;
	for (size_t i = 0; i + 1 < h.size(); i += 2) out.push_back(static_cast<std::uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16)));
	return out;
}
static std::string ToHex(const Bytes& b)
{
	static const char* d = "0123456789abcdef";
	std::string        s;
	for (auto c : b) {
		s += d[c >> 4];
		s += d[c & 15];
	}
	return s;
}
static std::vector<long long> Ints(const std::string& s)
{
	std::vector<long long> out;
	size_t                 pos = 0;
	while (pos < s.size()) {
		const auto comma = s.find(',', pos);
		out.push_back(std::stoll(s.substr(pos, comma - pos)));
		if (comma == std::string::npos) break;
		pos = comma + 1;
	}
	return out;
}
static std::string Join(const std::vector<int>& v)
{
	std::string s;
	for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + std::to_string(v[i]);
	return s;
}
static json::Value Json(const std::string& text)
{
	json::Value v;
	std::string why;
	Check(json::Parse(text, v, why), "json parse: " + why);
	return v;
}
static std::string Result(bool ok, const std::string& reason, const Bytes& a, const Bytes* b = nullptr)
{
	if (!ok) return "!" + reason;
	return b ? ToHex(a) + "|" + ToHex(*b) : ToHex(a);
}

static void Headers()
{
	const int n = std::stoi(E("hdr.count"));
	for (int i = 0; i < n; ++i) {
		const auto       k = "hdr" + std::to_string(i);
		std::vector<int> fields;
		std::set<int>    zeroes;
		for (auto f : Ints(E(k + ".fields"))) fields.push_back(static_cast<int>(f));
		for (auto z : Ints(E(k + ".zeroes"))) zeroes.insert(static_cast<int>(z));
		const auto h = BuildHeader(fields, zeroes);
		Check(ToHex(h) == E(k + ".bytes"), k + ": build_header " + ToHex(h) + " != " + E(k + ".bytes"));
		Bytes framed{ 0xaa, 0xbb };
		framed.insert(framed.end(), h.begin(), h.end());
		framed.push_back(1);
		framed.push_back(2);
		PropertyHeader ph;
		std::string    why;
		const bool     ok = ParseHeader(framed, 2, ph, why);
		const auto     got = ok ? Join(ph.fields) + "|" + Join({ ph.zeroed.begin(), ph.zeroed.end() }) + "|" + std::to_string(ph.end) : "!" + why;
		Check(got == E(k + ".parsed"), k + ": parse_header " + got + " != " + E(k + ".parsed"));
	}
	PropertyHeader ph;
	std::string    why;
	Check(!ParseHeader(Hex("0002"), 0, ph, why), "a header without its last fragment is refused (no read past the end)");
}

static void AddFields()
{
	const int n = std::stoi(E("add.count"));
	for (int i = 0; i < n; ++i) {
		const auto k = "add" + std::to_string(i);
		auto       data = Hex(E(k + ".in"));
		std::string reason;
		const bool  ok = AddField(data, std::stoull(E(k + ".pos")), Ints(E(k + ".sizes")), std::stoi(E(k + ".field")), Hex(E(k + ".value")), reason);
		const auto  got = Result(ok, reason, data);
		Check(got == E(k + ".out"), k + ": add_field " + got + " != " + E(k + ".out"));
	}
}

static void ReplaceExports()
{
	const auto ua = Hex(E("tile.uasset")), ux = Hex(E("tile.uexp"));
	const int  n = std::stoi(E("rep.count"));
	for (int i = 0; i < n; ++i) {
		const auto  k = "rep" + std::to_string(i);
		auto        a = ua, x = ux;
		std::string reason;
		const bool  ok = ReplaceExport(a, x, std::stoll(E(k + ".off")), std::stoll(E(k + ".size")), Hex(E(k + ".new")), reason);
		Check(Result(ok, reason, a, &x) == E(k + ".out"), k + ": replace_export " + Result(ok, reason, a, &x).substr(0, 120));
		if (!ok) Check(a == ua && x == ux, k + ": a refused replace_export leaves both files unchanged");
	}
	auto        a = Hex(E("repbulk.uasset")), x = ux;
	std::string reason;
	const bool  ok = ReplaceExport(a, x, 400, 18, Hex("00"), reason);
	Check(Result(ok, reason, a) == E("repbulk.out"), "replace_export BulkDataStartOffset twice: " + Result(ok, reason, a).substr(0, 120));
}

static void Rewrites()
{
	const auto ua = Hex(E("tile.uasset")), ux = Hex(E("tile.uexp"));
	for (const char* k : { "rw", "rwsha", "rwhdr", "rwstored" }) {
		const auto                 doc = Json(E(std::string(k) + ".json"));
		std::vector<LayoutRewrite> rewrites;
		for (const auto& r : doc.items) rewrites.push_back(LayoutRewriteFromJson(r));
		auto        a = ua, x = ux;
		std::string reason;
		const bool  ok = RewritePackage(a, x, rewrites, reason);
		Check(Result(ok, reason, a, &x) == E(std::string(k) + ".out"), std::string(k) + ": rewrite_package " + Result(ok, reason, a, &x).substr(0, 160));
		if (!ok) Check(a == ua && x == ux, std::string(k) + ": a refused rewrite leaves both files unchanged");
	}
	const auto data = Hex(E("msha.data"));
	const int  n = std::stoi(E("msha.count"));
	for (int i = 0; i < n; ++i) {
		const auto                                  k = "msha" + std::to_string(i);
		std::vector<std::pair<long long, long long>> mask;
		for (const auto& m : Json(E(k + ".mask")).items) mask.emplace_back(m.items[0].i, m.items[1].i);
		Check(MaskedSha256(data, mask) == E(k + ".out"), k + ": masked_sha256");
	}
}

static void Swaps()
{
	PackageSummary s;
	std::string    why;
	Check(ReadPackageSummary(Hex(E("pkg.in")), s, why), "package_summary reads the synthetic package: " + why);
	std::string got;
	for (const char* name : kOffsetFields) {
		const auto* f = s.Find(name);
		got += (got.empty() ? "" : ",") + std::string(name) + "=" + (f ? std::to_string(f->value) + "@" + std::to_string(f->at) : std::string("?"));
	}
	got += ",names_end=" + std::to_string(s.namesEnd) + ",end=" + std::to_string(s.end);
	Check(got == E("pkg.summary"), "package_summary offsets " + got);
	const int n = std::stoi(E("swap.count"));
	for (int i = 0; i < n; ++i) {
		const auto               k = "swap" + std::to_string(i);
		std::vector<ImportSwap>  swaps;
		for (const auto& sw : Json(E(k + ".json")).items) swaps.push_back(ImportSwapFromJson(sw));
		auto        ua = Hex(E(k + ".in"));
		const auto  stock = ua;
		std::string reason;
		const bool  ok = SwapImports(ua, swaps, reason);
		Check(Result(ok, reason, ua) == E(k + ".out"), k + " (" + E(k + ".label") + "): swap_imports " + Result(ok, reason, ua).substr(0, 160));
		if (!ok) Check(ua == stock, k + ": a refused swap leaves the .uasset unchanged");
	}
}

static Sizes SizesFromJson(const json::Value& a_doc)
{
	Sizes s;
	for (size_t k = 0; k < a_doc.keys.size(); ++k) s.items.emplace_back(a_doc.keys[k], a_doc.items[k].Number());
	return s;
}

static void Floats()
{
	const auto uexp = Hex(E("bpf.uexp"));
	const auto sizes = SizesFromJson(Json(E("bpf.sizes")));
	const int  n = std::stoi(E("bpf.count"));
	for (int i = 0; i < n; ++i) {
		const auto  k = "bpf" + std::to_string(i);
		const auto  row = LayoutRowFromJson(Json(E(k + ".json")));
		auto        x = uexp;
		std::string reason;
		const bool  ok = LayoutFloats(x, "WBP_Test", row, sizes, reason);
		Check(Result(ok, reason, x) == E(k + ".out"), k + ": bp_float " + Result(ok, reason, x).substr(0, 160));
		if (!ok) Check(x == uexp, k + ": a refused bp_float leaves the .uexp unchanged");
	}
	for (const char* k : { "lp", "lpfail" }) {
		PackData data;
		data.layout["WBP_Test"] = LayoutRowFromJson(Json(E(std::string(k) + ".json")));
		data.layoutPackages.push_back("WBP_Test");
		auto        a = Hex(E("tile.uasset")), x = Hex(E("tile.uexp"));
		const auto  sa = a, sx = x;
		std::string reason;
		const bool  ok = LayoutPackage(a, x, "WBP_Test", data, sizes, reason);
		Check(Result(ok, reason, a, &x) == E(std::string(k) + ".out"), std::string(k) + ": layout_package " + Result(ok, reason, a, &x).substr(0, 160));
		if (!ok) Check(a == sa && x == sx, std::string(k) + ": a refused layout_package leaves both files unchanged");
	}
}

static void SizesValues()
{
	Sizes       s;
	std::string error;
	const auto& ini = E("sizes.ini");
	Check(LoadSizes(&ini, s, error), "LoadSizes reads [Sizes]: " + error);
	Check(json::Dump(UsedSizes(s)) == E("sizes.used"), "used_sizes(load_sizes(file)) as JSON:\n" + json::Dump(UsedSizes(s)));
	Check(LoadSizes(nullptr, s, error), "LoadSizes without a file");
	Check(json::Dump(UsedSizes(s)) == E("sizes.default"), "used_sizes(SIZES) as JSON");
	const std::string bad = "[Sizes]\nmagicRowHeight = 401\n";
	Check(!LoadSizes(&bad, s, error) && error.find("needs a number from 1 to 400") != std::string::npos, "an out-of-range size is refused: " + error);
}

static PackData HeaderData()
{
	PackData data;
	for (const auto& [package, asset] : std::vector<std::pair<std::string, std::string>>{
			 { "WBP_ModernMenu_ContainerInventory_MainContent", "UI/C/WBP_ModernMenu_ContainerInventory_MainContent" },
			 { "WBP_ModernMenu_MagicMenu", "UI/M/WBP_ModernMenu_MagicMenu" },
			 { "WBP_OriginalMenu_Inventory_FilterScrollbox", "UI/F/WBP_OriginalMenu_Inventory_FilterScrollbox" } }) {
		data.layoutPackages.push_back(package);
		data.layout[package].asset = asset;
	}
	return data;
}

static void Manifests()
{
	const auto  data = HeaderData();
	Sizes       sizes;
	std::string error;
	LoadSizes(nullptr, sizes, error);
	auto entry = json::MakeObject();
	json::Set(entry, "recipe", json::MakeString("layout"));
	json::Set(entry, "stock_uexp_sha256", json::MakeString(std::string(64, 'a')));
	json::Set(entry, "baked_uexp_sha256", json::MakeString(std::string(64, 'b')));
	std::vector<PackFileHash> files;
	for (const char* ext : { "pak", "utoc", "ucas" }) {
		std::string body;
		for (int r = 0; r < 3; ++r) body += ext;
		files.push_back({ std::string("MRTest_P.") + ext, Sha256Hex(reinterpret_cast<const std::uint8_t*>(body.data()), body.size()) });
	}
	const std::string c = "UI/C/WBP_ModernMenu_ContainerInventory_MainContent", m = "UI/M/WBP_ModernMenu_MagicMenu", f = "UI/F/WBP_OriginalMenu_Inventory_FilterScrollbox";
	auto colourRecord = [&](bool a_sizes, bool a_filter) {
		BuildRecord r;
		r.styleOrder.push_back("menuTitle");
		r.style["menuTitle"] = model::Value{ 3, { 1, 2, 3 } };
		json::Set(r.textures, c, entry);
		json::Set(r.textures, m, entry);
		if (a_filter) json::Set(r.textures, f, entry);
		if (a_sizes) r.SetSizes(sizes);
		return r;
	};
	BuildRecord layout;
	layout.layoutPack = true;
	layout.SetSizes(sizes);
	for (const auto& a : { c, m, f }) json::Set(layout.textures, a, entry);
	auto colour = colourRecord(true, true);
	colour.Skip(f, "layout", "r");
	BuildRecord noheader;
	noheader.layoutPack = true;
	noheader.SetSizes(sizes);
	json::Set(noheader.textures, c, entry);
	const std::pair<const char*, BuildRecord> cases[] = { { "layout", layout }, { "colour", colour }, { "nosizes", colourRecord(false, false) }, { "noheader", noheader } };
	for (const auto& [key, record] : cases) {
		const auto text = json::Dump(Manifest("MRTest_P", "tools/make_dark_pack.py", record, files, &data)) + "\n";
		Check(text == E(std::string("man.") + key), std::string("write_manifest ") + key + ":\n" + text);
	}
}

static void DataFiles()
{
	const std::vector<std::pair<std::string, std::string>> files{
		{ "b.json", R"({"menu": "B", "order": 2, "import_swaps": {"WBP_P": [{"from_package": "/Game/A", "from_object": "A", "from_class": "Material", "to_package": "/Game/T", "to_object": "T", "to_class": "Texture2D"}]}})" },
		{ "a.json", R"({"menu": "A", "order": 5, "import_swaps": {"WBP_P": [{"from_package": "/Game/Late", "from_object": "Late", "from_class": "Material", "to_package": "/Game/T", "to_object": "T", "to_class": "Texture2D"}]}})" },
		{ "layout.json", R"({"menu": "Layout", "order": 0, "layout": {"WBP_Z": {"asset": "UI/WBP_Z", "floats": [{"widget": "w", "offset": 4, "stock": 36, "size": "magicRowHeight"}, {"widget": "d", "offset": 8, "stock": 1.5, "type": "double", "value": 2}], "bytes": [{"widget": "b", "offset": 1, "stock": 0, "value": 1}], "rewrites": [{"widget": "r", "export_start": 3, "export_size": 9, "header_pos": 1, "stock_fields": [0, 2], "stock_zeroes": [2], "add": [[3, -75.0]], "add_bytes": [[1, "0a0b"]], "field_sizes": [4, 4, 4, 4], "sha_mask": [[6, 16]], "stock_export_sha256": "ab"}]}, "WBP_A": {"asset": "UI/WBP_A"}}})" },
	};
	PackData    data;
	std::string error;
	Check(LoadPackData(files, data, error), "LoadPackData reads layout and import_swaps: " + error);
	Check(data.layoutPackages == std::vector<std::string>{ "WBP_Z", "WBP_A" }, "layout packages keep the file's key order");
	Check(data.layout.count("WBP_Z") && data.layout["WBP_Z"].asset == "UI/WBP_Z", "layout row asset");
	if (data.layout.count("WBP_Z")) {
		const auto& z = data.layout["WBP_Z"];
		Check(z.floats.size() == 2 && z.floats[0].size == "magicRowHeight" && z.floats[0].hasSize && !z.floats[0].isDouble && z.floats[1].isDouble && !z.floats[1].hasSize && z.floats[1].value == 2,
			"layout floats: size key, fixed value, double");
		Check(z.floats.size() == 2 && z.floats[0].stock.type == json::Type::Int && z.floats[1].stock.type == json::Type::Float, "stock values keep int/float for the messages");
		Check(z.bytes.size() == 1 && z.bytes[0].offset == 1 && z.bytes[0].value == 1, "layout bytes");
		Check(z.rewrites.size() == 1 && z.rewrites[0].exportStart == 3 && z.rewrites[0].headerPos == 1 && z.rewrites[0].stockZeroes == std::vector<int>{ 2 } &&
				  z.rewrites[0].add.size() == 1 && z.rewrites[0].add[0].second == -75.0 && z.rewrites[0].addBytes.size() == 1 && z.rewrites[0].addBytes[0].second == Bytes{ 10, 11 } &&
				  z.rewrites[0].shaMask.size() == 1 && z.rewrites[0].shaMask[0].second == 16,
			"layout rewrite descriptor");
	}
	Check(data.swaps.count("WBP_P") && data.swaps["WBP_P"].size() == 1 && data.swaps["WBP_P"][0].fromObject == "Late", "import_swaps: the later menu (by order) replaces the list");
	Check(data.importSwaps.count("WBP_P") == 1, "importSwaps still names the package");
	auto twice = files;
	twice.push_back({ "zz.json", R"({"layout": {"WBP_A": {"asset": "x"}}})" });
	Check(!LoadPackData(twice, data, error) && error.find("layout package WBP_A is listed twice") != std::string::npos, "a layout package listed twice is refused");
	Check(LayoutNeededNames(HeaderData()) == std::vector<std::string>{ "WBP_ModernMenu_ContainerInventory_MainContent", "WBP_ModernMenu_MagicMenu", "WBP_OriginalMenu_Inventory_FilterScrollbox" },
		"needed_names(colours=False, layout=True): the layout packages, sorted");
}

static void Entries()
{
	// layout_entries over a synthetic package: the stock floats bake, an unextracted package is skipped and listed, a staged
	// colour package is left alone
	PackData data;
	Bytes    uexp(32, 0);
	const float stock = 36.0f;
	std::memcpy(uexp.data() + 4, &stock, 4);
	for (const auto& name : { std::string("WBP_One"), std::string("WBP_Gone"), std::string("WBP_Colour") }) {
		data.layoutPackages.push_back(name);
		auto& row = data.layout[name];
		row.asset = "UI/" + name;
		LayoutFloat fl;
		fl.offset = 4;
		fl.stock = json::Value{};
		fl.stock.type = json::Type::Int;
		fl.stock.i = 36;
		fl.hasSize = true;
		fl.size = "magicRowHeight";
		fl.widget = "Row";
		row.floats.push_back(fl);
	}
	Sizes       sizes;
	std::string error;
	LoadSizes(nullptr, sizes, error);
	BuildRecord record;
	json::Set(record.textures, "UI/WBP_Colour", json::MakeObject());
	std::vector<StagedFiles> staged;
	LayoutEntries(data, sizes, [&](const std::string& a_asset, PackageFiles& a_out) {
		if (a_asset != "UI/WBP_One" && a_asset != "UI/WBP_Colour") return false;
		a_out.uasset = Bytes(8, 1);
		a_out.uexp = uexp;
		return true;
	}, record, staged);
	Check(staged.size() == 1 && staged[0].asset == "UI/WBP_One", "layout_entries stages the extracted layout-only package");
	Check(record.textures.keys == std::vector<std::string>{ "UI/WBP_Colour", "UI/WBP_One" }, "textures: the colour entry, then the layout entry");
	if (const auto* e = record.textures.Find("UI/WBP_One")) {
		Check(e->keys == std::vector<std::string>{ "recipe", "stock_uexp_sha256", "baked_uexp_sha256" } && e->Find("recipe")->s == "layout", "a layout entry has the Python keys");
		Check(e->Find("stock_uexp_sha256")->s == Sha256Hex(uexp), "stock_uexp_sha256");
		if (!staged.empty()) Check(e->Find("baked_uexp_sha256")->s == Sha256Hex(staged[0].files.uexp), "baked_uexp_sha256");
	}
	Check(record.skipped.items.size() == 1 && record.skipped.items[0].Find("asset")->s == "UI/WBP_Gone" && record.skipped.items[0].Find("recipe")->s == "layout" &&
			  record.skipped.items[0].Find("reason")->s == "not extracted from the game (game update or wrong folder?)",
		"an unextracted layout package is skipped and listed");
	if (!staged.empty()) {
		float baked = 0;
		std::memcpy(&baked, staged[0].files.uexp.data() + 4, 4);
		Check(baked == 36.0f, "the default magicRowHeight (36) baked");
	}
	// pipeline._build: a colour asset whose sizes were refused lists a layout row after its entry
	BuildRecord r;
	BakedAsset  b;
	b.entry = json::MakeObject();
	b.layoutSkipped = true;
	b.layoutReason = "WBP_X: stock size 1.0 at byte 4 (w), expected 2 (game update? re-scan the asset)";
	r.Add("UI/WBP_X", b, {});
	Check(r.textures.Find("UI/WBP_X") && r.skipped.items.size() == 1 && r.skipped.items[0].Find("recipe")->s == "layout" &&
			  r.skipped.items[0].Find("reason")->s == b.layoutReason,
		"BuildRecord::Add lists the refused sizes of a baked colour asset");
}

int main()
{
	for (const auto& [k, v] : kExpect) expect[k] = v;
	Headers();
	AddFields();
	ReplaceExports();
	Rewrites();
	Swaps();
	Floats();
	SizesValues();
	Manifests();
	DataFiles();
	Entries();
	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
