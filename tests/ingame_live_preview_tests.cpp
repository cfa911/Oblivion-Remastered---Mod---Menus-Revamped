// In-game Pack Builder Phase 4a: the pure part of the live art preview (src/builder/LivePreviewCore.h).
// - a role's keys, the changed keys inside the live roles, the texture rows that read them (the real data files)
// - one texture's preview bytes are the bytes the build writes (BakeAsset + InjectMips, read back) on a synthetic package
// - the per-mip layout the GPU copy uses, the DXGI formats a stock format may have, the UObject path of an asset
// - the stock cache stamp and the latest-wins request slot
// Phase 4b: every role live; the widget colour literals a change affects and their colour (equal to the bake's bytes on the
// real data), the stock literal check, colour slots in an object, the mip window of a streamed texture, the re-paint memo
// Byte parity of the preview with the build on the game's assets: tests/ingame_pack_parity_tests.py (driver "preview").
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
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
		const auto liveKeys = live::LiveKeys();
		Check(std::find(liveKeys.begin(), liveKeys.end(), "columnLine") != liveKeys.end(), "the lines role's keys are live");

		Values before, after;
		Check(PackValues(nullptr, before, error), "default values");
		after = before;
		after["columnLine"] = model::Value{ 3, { 200, 10, 10 } };
		after["rowText"] = model::Value{ 3, { 1, 2, 3 } };
		Check(live::ChangedKeys(before, after, live::RoleKeys("lines")) == std::vector<std::string>{ "columnLine" }, "changed keys inside a scope only");
		Check(live::ChangedKeys(before, after, {}) == std::vector<std::string>{ "columnLine", "rowText" }, "an empty scope lists every changed key (sorted)");
		Check(live::ChangedKeys(before, before, {}).empty(), "nothing changed, no keys");
		after = before;
		after.erase("columnLine");
		Check(live::ChangedKeys(before, after, live::RoleKeys("lines")) == std::vector<std::string>{ "columnLine" }, "a key that went missing counts as changed");
	}

	// ---- the texture rows a change affects (the real data files) ----
	{
		const auto menus = std::filesystem::path(__FILE__).parent_path().parent_path() / "tools/menus";
		std::vector<std::pair<std::string, std::string>> files;
		for (const auto& e : std::filesystem::directory_iterator(menus))
			if (e.path().extension() == ".json") files.emplace_back(e.path().filename().string(), ReadText(e.path()));
		PackData data;
		Check(LoadPackData(files, data, error), "loads tools/menus");
		const auto hits = live::AffectedTextures(data, live::RoleKeys("lines"));
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

	// ==== Phase 4b ====
	const auto menusDir = std::filesystem::path(__FILE__).parent_path().parent_path() / "tools/menus";
	PackData   real;
	{
		std::vector<std::pair<std::string, std::string>> files;
		for (const auto& e : std::filesystem::directory_iterator(menusDir))
			if (e.path().extension() == ".json") files.emplace_back(e.path().filename().string(), ReadText(e.path()));
		Check(LoadPackData(files, real, error), "4b: loads tools/menus");
	}

	// ---- every role is live ----
	{
		const auto& roles = live::LiveRoles();
		Check(roles.size() == model::Roles().size(), "4b: every role of the model is live");
		bool all = true;
		for (const auto& r : model::Roles()) all = all && std::find(roles.begin(), roles.end(), std::string(r.key)) != roles.end();
		Check(all, "4b: LiveRoles lists each model role");
		const auto keys = live::LiveKeys();
		bool members = true;
		for (const auto& r : model::Roles())
			for (const auto& m : r.members) members = members && std::find(keys.begin(), keys.end(), m) != keys.end();
		Check(members, "4b: every role member is a live key");
	}

	// ---- owner names ----
	Check(live::OwnerName(live::LiteralKind::Widget, "text (label)") == "text", "widget label: first word");
	Check(live::OwnerName(live::LiteralKind::Widget, "QuestDetails default colour") == "QuestDetails", "widget label with words after the name");
	Check(live::OwnerName(live::LiteralKind::Widget, "inv_entry_name") == "inv_entry_name", "a plain widget name");
	Check(live::OwnerName(live::LiteralKind::Material, "BorderColor - Dark") == "BorderColor - Dark", "material parameter with spaces");
	Check(live::OwnerName(live::LiteralKind::Material, "Color (instance override)") == "Color", "material parameter before the note");
	Check(live::OwnerName(live::LiteralKind::Material, "orange effect colour (CachedExpressionData)").empty() &&
			live::OwnerName(live::LiteralKind::Material, "orange effect colour (shader map default)").empty(),
		"a material label that names no parameter");
	Check(live::OwnerName(live::LiteralKind::Widget, "").empty(), "no label, no owner");

	// ---- the literals a change affects (the real data) ----
	{
		const auto text = live::AffectedLiterals(real, { "rowText" });
		bool found = false, onlyText = true, widgets = true;
		for (const auto& l : text) {
			onlyText = onlyText && l.colour == "rowText";
			widgets = widgets && l.kind == live::LiteralKind::Widget;
			if (l.asset.ends_with("WBP_OriginalMenu_InventoryEntry") && l.owner == "inv_entry_name" && l.offset == 11935) found = true;
		}
		Check(text.size() > 100, "rowText reaches more than a hundred literals");
		Check(found, "rowText lists inv_entry_name of WBP_OriginalMenu_InventoryEntry at 11935");
		Check(onlyText && widgets, "only rowText widget literals");

		const auto track = live::AffectedLiterals(real, { "Skin.scrollTrack" });
		bool dark = false, light = false, materials = !track.empty();
		for (const auto& l : track) {
			materials = materials && l.kind == live::LiteralKind::Material;
			if (l.asset.ends_with("M_UI_Scrollbar_Vert_Reworked")) dark = dark || l.owner == "DarkColor", light = light || l.owner == "LightColor";
		}
		Check(materials && dark && light, "Skin.scrollTrack lists the scrollbar material's DarkColor and LightColor");
		Check(live::AffectedLiterals(real, { "steelGlow", "white" }).empty(), "constant colours never change: no literals");
		Check(live::AffectedLiterals(real, {}).empty(), "no keys, no literals");

		// every rich_row row is listed with the table's offset and stock
		size_t richRows = 0, richFound = 0;
		for (const auto& [asset, recipe] : real.textures) {
			const auto [kind, arg] = Partition(recipe);
			if (kind != "rich_row") continue;
			++richRows;
			const auto [row, colour] = Partition(arg);
			const auto& entry = real.richRows.at("DT_UI_MagicMenu").at(std::string(row));
			for (const auto& l : live::AffectedLiterals(real, { std::string(colour) }))
				if (l.kind == live::LiteralKind::RichRow && l.owner == row && l.asset == asset && l.offset == entry.offset && l.stock == entry.stock) {
					++richFound;
					break;
				}
		}
		Check(richFound == richRows, "every rich_row row of the data is listed with its table offset and stock (none today: RMI-086)");
		{   // a synthetic rich_row row: listed with the table's offset and stock, its colour as the bake writes it
			PackData d;
			d.richRows["DT_UI_MagicMenu"]["RT_BlueMed"] = RichRow{ 425, { 0.0513, 0.0648, 0.1441, 1.0 } };
			d.richRows["DT_UI_MagicMenu"]["Default"] = RichRow{ 69, { 0.0723, 0.0382, 0.0284, 1.0 } };
			d.textures = { { "UI/Modern/GameMenuLayer/Magic/DT_UI_MagicMenu", "rich_row:RT_BlueMed:accentValue" },
				{ "UI/Modern/GameMenuLayer/Magic/DT_UI_MagicMenu", "rich_row:Default:rowText" } };
			const auto rl = live::AffectedLiterals(d, { "accentValue", "rowText" });
			Check(rl.size() == 2 && rl[0].kind == live::LiteralKind::RichRow && rl[0].owner == "RT_BlueMed" && rl[0].offset == 425 && rl[1].owner == "Default",
				"both rows of one table are listed");
			Values v;
			PackValues(nullptr, v, error);
			Bytes uexp(600, 0);
			for (const auto& [o, st] : { std::pair<int, std::array<double, 4>>{ 425, { 0.0513, 0.0648, 0.1441, 1.0 } }, { 69, { 0.0723, 0.0382, 0.0284, 1.0 } } }) {
				const float f[4] = { float(st[0]), float(st[1]), float(st[2]), float(st[3]) };
				std::memcpy(uexp.data() + o, f, 16);
			}
			Bytes       out;
			std::string skip;
			std::array<float, 4> c{};
			Check(BakePackage("rich_row:RT_BlueMed:accentValue", "DT_UI_MagicMenu", uexp, v, d, out, skip) && live::LiteralColour(rl[0], v, 0.25f, c) &&
					std::memcmp(out.data() + 425, c.data(), 16) == 0,
				"a rich row's colour equals the bake's bytes (alpha 1)");
		}

		// count: each non-constant entry of each package row once (rows of the same asset once)
		std::vector<std::string> all;
		for (const auto& [k, v] : [] { Values d; std::string e; PackValues(nullptr, d, e); return d; }()) all.push_back(k);
		size_t expected = 0;
		std::set<std::string> seen;
		for (const auto& [asset, recipe] : real.textures) {
			const auto [kind, arg] = Partition(recipe);
			if (kind == "rich_row") {
				if (seen.insert(asset + "|" + std::string(Partition(arg).first)).second) ++expected;
			} else if ((kind == "bp_colour" || kind == "material_colour") && seen.insert(asset).second) {
				model::Value unused;
				for (const auto& c : real.blueprints.at(std::string(arg)))
					if (!recipes::ConstantColour(c.colour, unused)) ++expected;
			}
		}
		Check(live::AffectedLiterals(real, all).size() == expected, "every key: each changeable package literal listed once");
	}

	// ---- the literal colours equal the bake's bytes (the real data, a synthetic stock .uexp per package) ----
	{
		Values values;
		PackValues(nullptr, values, error);
		for (auto& [k, v] : values) {   // a scenario far from the defaults
			if (v.n == 3) v.v = { std::fmod(v.v[0] + 97, 256), std::fmod(v.v[1] + 41, 256), std::fmod(v.v[2] + 173, 256), 0 };
			else if (v.n == 1) v.v[0] = v.v[0] * 0.5;
			else if (v.n == 4) v.v = { v.v[0] * 0.7, v.v[1] * 0.9, std::min(1.0, v.v[2] + 0.1), v.v[3] };
		}
		std::vector<std::string> all;
		for (const auto& [k, v] : values) all.push_back(k);
		const auto lits = live::AffectedLiterals(real, all);
		size_t compared = 0, equal = 0, packages = 0, baked = 0;
		std::set<std::string> done;
		for (const auto& [asset, recipe] : real.textures) {
			const auto [kind, arg] = Partition(recipe);
			if ((kind != "bp_colour" && kind != "material_colour" && kind != "rich_row") || done.count(asset)) continue;
			done.insert(asset);
			++packages;
			std::int64_t end = 0;
			std::vector<std::pair<std::int64_t, std::array<double, 4>>> colours;
			if (kind == "rich_row") {
				const auto& e = real.richRows.at("DT_UI_MagicMenu").at(std::string(Partition(arg).first));
				colours.emplace_back(e.offset, e.stock);
			} else
				for (const auto& c : real.blueprints.at(std::string(arg))) colours.emplace_back(c.offset, c.stock);
			for (const auto& [o, s] : colours) end = std::max(end, o + 16);
			const auto lit = real.literals.find(std::string(arg));
			if (kind != "rich_row" && lit != real.literals.end())
				for (const auto& r : lit->second) end = std::max<std::int64_t>(end, r.offset + 8);
			Bytes uexp(static_cast<size_t>(end) + 16, 0);
			for (const auto& [o, s] : colours) {
				const float f[4] = { float(s[0]), float(s[1]), float(s[2]), float(s[3]) };
				std::memcpy(uexp.data() + o, f, 16);
			}
			if (kind != "rich_row" && lit != real.literals.end())
				for (const auto& r : lit->second) {
					if (r.type == "double") std::memcpy(uexp.data() + r.offset, &r.stock, 8);
					else if (r.type == "int32") {
						const auto v = static_cast<std::int32_t>(r.stock);
						std::memcpy(uexp.data() + r.offset, &v, 4);
					} else {
						const auto v = static_cast<float>(r.stock);
						std::memcpy(uexp.data() + r.offset, &v, 4);
					}
				}
			Bytes       out;
			std::string skip;
			if (!BakePackage(recipe, AssetName(asset), uexp, values, real, out, skip)) continue;
			++baked;
			for (const auto& l : lits) {
				if (l.asset != asset) continue;
				std::array<float, 4> stock{}, colour{};
				++compared;
				if (!live::StockLiteral(l, uexp, stock) || !live::LiteralColour(l, values, stock[3], colour)) continue;
				if (std::memcmp(out.data() + l.offset, colour.data(), 16) == 0) ++equal;
				else std::printf("  differs: %s %s at %lld\n", asset.c_str(), l.owner.c_str(), static_cast<long long>(l.offset));
			}
		}
		std::printf("  literal parity: %zu packages (%zu baked), %zu literals, %zu equal\n", packages, baked, compared, equal);
		Check(baked == packages, "every package row bakes on its synthetic stock");
		Check(compared > 200 && equal == compared, "every literal colour equals the bytes the bake writes");
	}

	// ---- the stock literal ----
	{
		live::LiveLiteral l;
		l.offset = 4;
		l.stock = { 0.0723, 0.0382, 0.0284, 1.0 };
		Bytes                uexp(24, 0);
		const float          f[4] = { 0.0723f, 0.0382f, 0.0284f, 1.0f };
		std::memcpy(uexp.data() + 4, f, 16);
		std::array<float, 4> got{};
		Check(live::StockLiteral(l, uexp, got) && got[0] == f[0] && got[3] == 1.0f, "the stock literal is read at its offset");
		l.offset = 12;
		Check(!live::StockLiteral(l, uexp, got), "past the end: refused");
		l.offset = 0;
		Check(!live::StockLiteral(l, uexp, got), "not the stock value: refused (a game update moved it)");
		l.offset = -1;
		Check(!live::StockLiteral(l, uexp, got), "a negative offset: refused");
	}

	// ---- class path, same colour, colour slots ----
	Check(live::ClassPath("UI/Modern/X/WBP_A") == "/Game/UI/Modern/X/WBP_A.WBP_A_C", "class path of a widget blueprint");
	{
		const std::array<float, 4> ink{ 0.0723f, 0.0382f, 0.0284f, 1.0f };
		const float                near[4] = { 0.0725f, 0.0380f, 0.0284f, 1.0f }, far[4] = { 0.08f, 0.0382f, 0.0284f, 1.0f }, alpha[4] = { 0.0723f, 0.0382f, 0.0284f, 0.5f };
		Check(live::SameColour(near, ink), "within 5e-4: the same colour");
		Check(!live::SameColour(far, ink) && !live::SameColour(alpha, ink), "another channel or alpha: another colour");
		std::vector<std::uint8_t> bytes(96, 0);
		std::memcpy(bytes.data() + 8, near, 16);
		std::memcpy(bytes.data() + 40, ink.data(), 16);
		std::memcpy(bytes.data() + 62, ink.data(), 16);   // unaligned: not a property
		std::memcpy(bytes.data() + 80, alpha, 16);
		const auto slots = live::ColourSlots(bytes.data(), bytes.size(), { ink });
		Check(slots == std::vector<size_t>{ 8, 40 }, "colour slots: aligned matches only, alpha checked");
		Check(live::ColourSlots(bytes.data(), bytes.size(), { ink, { 0.0723f, 0.0382f, 0.0284f, 0.5f } }) == std::vector<size_t>{ 8, 40, 80 }, "several candidates");
		Check(live::ColourSlots(bytes.data(), 50, { ink }) == std::vector<size_t>{ 8 }, "a slot must fit in the object");
	}

	// ---- the colours the screen shows at the first text: the installed pack's (RMI-115) ----
	{
		Values file, dark;
		Check(PackValues(nullptr, dark, error), "Dark default values");
		file = dark;
		file["rowText"] = model::Value{ 3, { 0, 255, 0 } };
		Check(live::ShownValues(true, file, dark, nullptr).at("rowText").v[1] == 255, "Custom installed, no manifest: the colour file");
		Check(live::ShownValues(false, file, dark, nullptr).at("rowText").v[1] == dark.at("rowText").v[1], "Dark (or Classic) installed: Dark's colours, not the colour file");
		// owner run 2026-11-28: Custom installed, built with rowText 255,255,255, the file already saved with 0,255,0
		json::Value manifest;
		Check(json::Parse(R"({"style": {"rowText": [255.0, 255.0, 255.0], "detailsPage": 0.01}})", manifest, error), "manifest parses");
		const auto shown = live::ShownValues(true, file, dark, manifest.Find("style"));
		Check(shown.at("rowText").n == 3 && shown.at("rowText").v[1] == 255 && shown.at("rowText").v[0] == 255, "Custom installed: the pack's built colour wins over the saved file");
		Check(shown.at("detailsPage").n == 1 && std::fabs(shown.at("detailsPage").v[0] - 0.01) < 1e-9, "a single-number entry from the manifest");
		Check(shown.at("columnLine").v[0] == file.at("columnLine").v[0], "a key the manifest does not list keeps the file's value");
	}

	// ---- the game's text prefab (WBP_AltarTextBlock): its own Color variable and SetColor (RMI-115) ----
	{
		Check(live::SetterFor("Color") == "SetColor", "a prefab's Color goes through its SetColor (the prefab passes it to its inner text)");
		Check(live::SetterFor("ColorAndOpacity") == "SetColorAndOpacity", "TextBlock setter");
		Check(live::SetterFor("BrushColor") == "SetBrushColor" && live::SetterFor("Brush") == "SetBrushTintColor", "Border and Image setters");
		Check(live::SetterFor("ColorAndOpacityDelegate").empty(), "no setter for anything else");
		// a prefab paints whatever colour it shows now (only colours edited this session are ever posted); other widgets keep
		// the guard that leaves a colour the game set alone
		Check(live::ShouldPaint(true, false, false), "prefab: paint even when its colour is not a known one");
		Check(!live::ShouldPaint(true, true, true), "already the wanted colour: nothing to do");
		Check(!live::ShouldPaint(false, false, false), "other widget holding an unknown colour: left alone");
		Check(live::ShouldPaint(false, false, true), "other widget holding a known colour: painted");
	}

	// ---- the property that holds a colour slot (RMI-115) ----
	{
		// a TextBlock in game: the field list also names ColorAndOpacityDelegate over the same bytes (owner run 2026-11-28)
		const std::vector<live::FieldSpan> textBlock{ { "Text", 0x180, 0x18 }, { "ColorAndOpacityDelegate", 0x194, 0x20 },
			{ "ColorAndOpacity", 0x1A4, 0x14 }, { "Font", 0x1C0, 0x60 } };
		Check(live::PropertyFor(textBlock, 0x1A4) == "ColorAndOpacity", "a colour property wins over a delegate over the same bytes");
		Check(live::PropertyFor(textBlock, 0x1C8) == "Font", "no colour property: the first field that holds it");
		Check(live::PropertyFor(textBlock, 0x400).empty(), "no field holds it");
		const std::vector<live::FieldSpan> border{ { "Brush", 0x100, 0x90 }, { "BrushColor", 0x190, 0x10 } };
		Check(live::PropertyFor(border, 0x190) == "BrushColor", "BrushColor");
		Check(live::PropertyFor(border, 0x110) == "Brush", "Brush counts as a colour property");
	}

	// ---- mip window ----
	{
		int n = 0;
		Check(live::MipWindow(256, 256, 9, 256, 256, 9, n) == 0 && n == 9, "a fully resident texture: every mip");
		Check(live::MipWindow(256, 256, 9, 64, 64, 7, n) == 2 && n == 7, "streamed: the package mips from the resource's top size");
		Check(live::MipWindow(256, 256, 9, 64, 64, 9, n) == 2 && n == 7, "never more mips than the package has below");
		Check(live::MipWindow(256, 128, 9, 32, 16, 4, n) == 3 && n == 4, "a non-square texture");
		Check(live::MipWindow(100, 50, 1, 100, 50, 1, n) == 0 && n == 1, "a one-mip texture");
		Check(live::MipWindow(256, 256, 1, 128, 128, 1, n) == -1 && n == 0, "a size the package does not have: refused");
		Check(live::MipWindow(256, 256, 9, 48, 48, 1, n) == -1, "no mip of that size: refused");
		Check(live::MipWindow(256, 256, 9, 256, 128, 9, n) == -1, "another aspect: refused");
	}

	// ---- re-paint memo ----
	{
		int                    a = 0, b = 0;
		live::PaintMemo        memo;
		Check(!live::NeedsPaint(memo, nullptr, 1), "no resource: nothing to paint");
		Check(live::NeedsPaint(memo, &a, 1), "never painted: paint");
		memo = { &a, 1 };
		Check(!live::NeedsPaint(memo, &a, 1), "painted with this preview: no paint");
		Check(live::NeedsPaint(memo, &a, 2), "a newer preview: paint");
		Check(live::NeedsPaint(memo, &b, 1), "another resource (the menu was opened again): paint");
	}

	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
