// In-game Pack Builder Phase 4a: the pure part of the live art preview (src/builder/LivePreviewCore.h).
// - a role's keys, the changed keys inside the live roles, the texture rows that read them (the real data files)
// - one texture's preview bytes are the bytes the build writes (BakeAsset + InjectMips, read back) on a synthetic package
// - the per-mip layout the GPU copy uses, the DXGI formats a stock format may have, the UObject path of an asset
// - the stock cache stamp and the latest-wins request slot
// Byte parity of the preview with the build on the game's assets: tests/ingame_pack_parity_tests.py (driver "preview").
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "../src/builder/LivePreviewCore.h"

using namespace builder::pack;
namespace live = builder::pack::live;
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

// ---- a synthetic legacy texture package (copied from tests/ingame_pack_core_tests.cpp) ----
template <class T> static void Put(Bytes& b, T v)
{
	const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
	b.insert(b.end(), p, p + sizeof(T));
}

struct Synthetic
{
	Bytes uasset, uexp, ubulk;
};

static Synthetic MakeTexture(const char* format, int w, int h, const std::vector<std::pair<int, bool>>& mips)
{
	Synthetic s;
	Bytes&    a = s.uasset;
	Put<std::uint32_t>(a, 0x9E2A83C1);
	Put<std::int32_t>(a, -8);
	for (int i = 0; i < 5; ++i) Put<std::int32_t>(a, 0);
	const size_t totalAt = a.size();
	Put<std::int32_t>(a, 0);
	Put<std::int32_t>(a, 6);
	for (char c : std::string("/Game")) a.push_back(static_cast<std::uint8_t>(c));
	a.push_back(0);
	Put<std::uint32_t>(a, 0x80002000);
	for (int i = 0; i < 2 + 2 + 2 + 4 + 1 + 3 + 1; ++i) Put<std::int32_t>(a, 0);
	for (int i = 0; i < 16; ++i) a.push_back(0);
	Put<std::int32_t>(a, 1);
	Put<std::int32_t>(a, 1);
	Put<std::int32_t>(a, 9);
	for (int i = 0; i < 28 + 8; ++i) a.push_back(0);
	Put<std::uint32_t>(a, 0x11223344);
	for (int i = 0; i < 1 + 2 + 1 + 2 + 2 + 1; ++i) Put<std::int32_t>(a, 0);
	Put<std::int64_t>(a, 0);
	const size_t droAt = a.size();
	Put<std::int32_t>(a, 0);
	for (int i = 0; i < 24; ++i) a.push_back(0xAB);
	const auto tableAt = static_cast<std::int32_t>(a.size());
	std::memcpy(a.data() + droAt, &tableAt, 4);
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
		const int mw = std::max(1, w >> level), mh = std::max(1, h >> level);
		++level;
		Bytes&     to = bulk ? s.ubulk : x;
		const auto at = static_cast<std::int64_t>(to.size());
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
		Put<std::uint32_t>(a, bulk ? 0x10501u : 0x48u);
	}
	for (int i = 0; i < 12; ++i) x.push_back(0);
	Put<std::uint32_t>(x, 0x9E2A83C1);
	const auto total = static_cast<std::int32_t>(a.size());
	std::memcpy(a.data() + totalAt, &total, 4);
	return s;
}

static std::string ReadText(const std::filesystem::path& p)
{
	std::ifstream     in(p, std::ios::binary);
	std::stringstream s;
	s << in.rdbuf();
	return s.str();
}

// what the build writes for one texture row: BakeAsset's package, its mips read back
static Bytes BuildBody(const live::Target& t, const PackageFiles& stock, const Values& values, const PackData& data)
{
	BakedAsset  baked;
	std::string reason;
	if (BakeAsset(t.asset, t.recipe, stock, values, data, baked, reason) != BakeResult::Baked) return {};
	TexturePackage tex;
	if (!ParseTexture(baked.files.uasset, baked.files.uexp, baked.files.hasUbulk ? &baked.files.ubulk : nullptr, tex, reason)) return {};
	return GatherMips(tex, baked.files.uexp, baked.files.hasUbulk ? &baked.files.ubulk : nullptr);
}

int main()
{
	std::string error;

	// ---- roles and changed keys ----
	{
		const auto lines = live::RoleKeys("lines");
		Check(lines == std::vector<std::string>{ "rowSeparator", "columnLine", "Skin.ornament" }, "RoleKeys(lines) = the role's members in model order");
		Check(live::RoleKeys("no such role").empty(), "an unknown role has no keys");
		Check(live::LiveKeys() == lines, "4a: the live keys are the lines role's");

		Values before, after;
		Check(PackValues(nullptr, before, error), "default values");
		after = before;
		after["columnLine"] = model::Value{ 3, { 200, 10, 10 } };
		after["rowText"] = model::Value{ 3, { 1, 2, 3 } };
		Check(live::ChangedKeys(before, after, live::LiveKeys()) == std::vector<std::string>{ "columnLine" }, "changed keys inside the live roles only");
		Check(live::ChangedKeys(before, after, {}) == std::vector<std::string>{ "columnLine", "rowText" }, "an empty scope lists every changed key (sorted)");
		Check(live::ChangedKeys(before, before, {}).empty(), "nothing changed, no keys");
		after = before;
		after.erase("columnLine");
		Check(live::ChangedKeys(before, after, live::LiveKeys()) == std::vector<std::string>{ "columnLine" }, "a key that went missing counts as changed");
	}

	// ---- the texture rows a change affects (the real data files) ----
	{
		const auto menus = std::filesystem::path(__FILE__).parent_path().parent_path() / "tools/menus";
		std::vector<std::pair<std::string, std::string>> files;
		for (const auto& e : std::filesystem::directory_iterator(menus))
			if (e.path().extension() == ".json") files.emplace_back(e.path().filename().string(), ReadText(e.path()));
		PackData data;
		Check(LoadPackData(files, data, error), "loads tools/menus");
		const auto hits = live::AffectedTextures(data, live::LiveKeys());
		auto has = [&](const char* asset) {
			for (const auto& t : hits)
				if (t.asset == asset) return true;
			return false;
		};
		Check(hits.size() >= 10, "the lines role reaches at least ten textures");
		Check(has("Art/UI/Common/T_Line_V") && has("Art/UI/Common/T_Line_Brown") && has("Art/UI/Common/T_Line_02") &&
				has("Art/UI/Modern/GameMenuLayer/CharacterCreation/T_Character_Line_Long"),
			"columnLine and Skin.ornament textures are listed");
		bool onlyTextures = true, onlyReaders = true, ordered = true;
		size_t at = 0;
		for (const auto& t : hits) {
			const auto info = Recipe(t.recipe, data);
			onlyTextures = onlyTextures && info.kind == RecipeKind::Texture;
			bool reads = false;
			for (const auto& k : info.keys) reads = reads || k == "rowSeparator" || k == "columnLine" || k == "Skin.ornament";
			onlyReaders = onlyReaders && reads;
			while (at < data.textures.size() && data.textures[at].first != t.asset) ++at;
			ordered = ordered && at < data.textures.size() && data.textures[at].second == t.recipe;
		}
		Check(onlyTextures, "no package recipe (bp_colour, material_colour, rich_row) is listed");
		Check(onlyReaders, "every listed recipe reads a key of the role");
		Check(ordered, "listed in data order with the row's recipe");
		size_t expected = 0;
		for (const auto& [asset, recipe] : data.textures) {
			const auto info = Recipe(recipe, data);
			if (info.kind != RecipeKind::Texture) continue;
			for (const auto& k : info.keys)
				if (k == "rowSeparator" || k == "columnLine" || k == "Skin.ornament") {
					++expected;
					break;
				}
		}
		Check(hits.size() == expected, "every texture row that reads the role is listed once");
		Check(live::AffectedTextures(data, {}).empty(), "no keys, no textures");
		const auto icons = live::AffectedTextures(data, { "rowIcon" });
		bool noLines = true;
		for (const auto& t : icons) noLines = noLines && t.asset != "Art/UI/Common/T_Line_V";
		Check(!icons.empty() && noLines, "another key lists its own textures only");
	}

	// ---- the preview bytes equal the build's ----
	{
		PackData data;   // no data file needed by flat / skin_flat
		Values   values;
		PackValues(nullptr, values, error);
		values["columnLine"] = model::Value{ 3, { 201, 33, 77 } };
		values["Skin.ornament"] = model::Value{ 4, { 0.5, 0.25, 0.125, 1.0 } };
		auto         s = MakeTexture("DXT5", 8, 8, { { 64, false }, { 16, false }, { 16, false }, { 16, false } });
		PackageFiles stock{ s.uasset, s.uexp, {}, false };
		const live::Target flat{ "Art/UI/Common/T_Line_V", "flat:columnLine" };
		live::Preview      p;
		Check(live::BakePreview(flat, stock, values, data, {}, p, error), "a flat texture previews");
		const auto want = BuildBody(flat, stock, values, data);
		Check(!want.empty() && p.body == want, "preview body = the mips the build writes (flat:columnLine)");
		Check(p.asset == flat.asset && p.format == PixelFormat::DXT5 && p.width == 8 && p.height == 8 && p.mips.size() == 4, "preview format, size and mip count");
		Check(p.mips.size() == 4 && p.mips[0].size == 64 && p.mips[1].offset == 64 && p.mips[3].offset == 96 && p.mips[3].rows == 1, "preview mip slices");

		auto         s1 = MakeTexture("DXT5", 4, 32, { { 128, false } });   // skin_flat needs BC3 (FormatSupported)
		PackageFiles stock1{ s1.uasset, s1.uexp, {}, false };
		const live::Target skin{ "Art/UI/Common/T_Line_02", "skin_flat:ornament:235" };
		Check(live::BakePreview(skin, stock1, values, data, {}, p, error), "a skin_flat texture previews");
		const auto want1 = BuildBody(skin, stock1, values, data);
		Check(!want1.empty() && p.body == want1, "preview body = the mips the build writes (skin_flat:ornament)");
		values["Skin.ornament"] = model::Value{ 4, { 0.9, 0.1, 0.1, 1.0 } };
		Check(live::BakePreview(skin, stock1, values, data, {}, p, error) && p.body == BuildBody(skin, stock1, values, data) && p.body != want1,
			"a new colour gives new bytes, still the build's");

		auto         sb = MakeTexture("DXT5", 8, 8, { { 64, true }, { 16, false } });
		PackageFiles stockb{ sb.uasset, sb.uexp, sb.ubulk, true };
		Check(live::BakePreview(flat, stockb, values, data, {}, p, error) && p.body == BuildBody(flat, stockb, values, data) && p.mips.size() == 2,
			"a mip in the .ubulk previews like the build");

		Check(!live::BakePreview({ "UI/X/WBP_X", "bp_colour:WBP_X" }, stock, values, data, {}, p, error) && !error.empty(), "a package recipe is refused with a reason");
		Values missing = values;
		missing.erase("columnLine");
		Check(!live::BakePreview(flat, stock, missing, data, {}, p, error) && error.find("columnLine") != std::string::npos, "a missing value is refused like the build");
		PackageFiles broken{ Bytes(10), Bytes(10), {}, false };
		Check(!live::BakePreview(flat, broken, values, data, {}, p, error) && !error.empty(), "a broken stock package is refused");
	}

	// ---- mip layout for the GPU copy ----
	{
		std::vector<live::MipSlice> m;
		Check(live::MipLayout(PixelFormat::DXT5, 256, 256, 9, 87408, m) && m.size() == 9, "DXT5 256x256 9 mips");
		Check(m.size() == 9 && m[0].rowBytes == 1024 && m[0].rows == 64 && m[0].size == 65536 && m[8].offset == 87392 && m[8].size == 16 && m[8].width == 1 &&
				m[8].rows == 1 && m[8].rowBytes == 16,
			"DXT5 rows are block rows; mips below 4x4 are one block");
		Check(live::MipLayout(PixelFormat::DXT1, 4, 32, 1, 64, m) && m.size() == 1 && m[0].rowBytes == 8 && m[0].rows == 8, "DXT1 4x32");
		Check(live::MipLayout(PixelFormat::B8G8R8A8, 1024, 64, 1, 262144, m) && m[0].rowBytes == 4096 && m[0].rows == 64, "B8G8R8A8 1024x64");
		Check(live::MipLayout(PixelFormat::G8, 16, 8, 2, 160, m) && m.size() == 2 && m[1].rowBytes == 8 && m[1].rows == 4, "G8 two mips");
		Check(!live::MipLayout(PixelFormat::DXT5, 256, 256, 9, 87407, m) && m.empty(), "a body of another size is refused");
		Check(!live::MipLayout(PixelFormat::Unknown, 4, 4, 1, 16, m), "an unknown format is refused");
		Check(!live::MipLayout(PixelFormat::DXT5, 0, 4, 1, 16, m), "a zero size is refused");
	}

	// ---- formats and paths ----
	Check(live::FormatMatches(PixelFormat::DXT5, 76) && live::FormatMatches(PixelFormat::DXT5, 77) && live::FormatMatches(PixelFormat::DXT5, 78), "DXT5 = BC3");
	Check(live::FormatMatches(PixelFormat::DXT1, 70) && live::FormatMatches(PixelFormat::B8G8R8A8, 90) && live::FormatMatches(PixelFormat::B8G8R8A8, 87),
		"DXT1 = BC1, B8G8R8A8 = B8G8R8A8");
	Check(!live::FormatMatches(PixelFormat::DXT5, 70) && !live::FormatMatches(PixelFormat::DXT1, 76) && !live::FormatMatches(PixelFormat::DXT5, 83) &&
			!live::FormatMatches(PixelFormat::Unknown, 76),
		"other formats do not match");
	Check(live::ObjectPath("Art/UI/Common/T_Line_V") == "/Game/Art/UI/Common/T_Line_V.T_Line_V", "object path of a stock asset");
	Check(live::ObjectPath("T_X") == "/Game/T_X.T_X", "object path without folders");

	// ---- the stock cache stamp ----
	{
		const auto a = live::CacheStamp({ "T_A", "T_B" }, "paks 1"), b = live::CacheStamp({ "T_B", "T_A" }, "paks 1");
		Check(!a.empty() && a == b, "the stamp ignores the name order");
		Check(a != live::CacheStamp({ "T_A" }, "paks 1"), "other names, another stamp");
		Check(a != live::CacheStamp({ "T_A", "T_B" }, "paks 2"), "other paks, another stamp");
		Check(a.find(kGameBuild) != std::string::npos && a.find(kBuilderVersion) != std::string::npos, "the stamp names the game build and the builder version");
	}

	// ---- latest wins ----
	{
		live::LatestWins<int> slot;
		Check(!slot.Pending() && !slot.Take(), "empty slot");
		slot.Post(1);
		slot.Post(2);
		slot.Post(3);
		Check(slot.Pending(), "a request waits");
		const auto got = slot.Take();
		Check(got && *got == 3, "the worker takes the newest request");
		Check(!slot.Pending() && !slot.Take(), "older requests were dropped");
	}

	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
