// In-game Pack Builder Phase 3e: putting the built packs in place (src/builder/PackInstall.h), a port of
// tools/pack_builder/install.py, app.layout_enabled and app.summary.
// - the four files move together; the previous set becomes .bak (the one before that is dropped)
// - an incomplete build changes nothing; a replace failing half-way puts the previous set back
// - [Layout] bLayoutPack=0 turns the sizes off; anything else, or no entry, keeps them
#include <cstdio>
#include <map>
#include <string>
#include "../src/builder/PackInstall.h"

using namespace builder::pack;
namespace fs = std::filesystem;

unsigned checks = 0, failures = 0;
void Check(bool ok, const char* why)
{
	++checks;
	if (!ok) {
		++failures;
		std::printf("FAIL %s\n", why);
	}
}

struct Disk   // a fake folder tree: path -> content
{
	std::map<std::string, std::string> files;
	int                                failAt = -1, moves = 0;
	FileOps                            Ops()
	{
		FileOps ops;
		ops.exists = [this](const fs::path& p) { return files.count(p.generic_string()) > 0; };
		ops.replace = [this](const fs::path& a, const fs::path& b) {
			if (moves++ == failAt || !files.count(a.generic_string())) return false;
			files[b.generic_string()] = files[a.generic_string()];
			files.erase(a.generic_string());
			return true;
		};
		ops.remove = [this](const fs::path& p) { files.erase(p.generic_string()); };
		return ops;
	}
};

static void Built(Disk& d, const char* tag)
{
	for (const char* ext : { "pak", "utoc", "ucas", "json" }) d.files[std::string("out/MRCustom_P.") + ext] = tag;
}

int main()
{
	std::string error;
	{
		Disk d;
		Built(d, "new");
		Check(InstallPack("out", "pack", "MRCustom_P", d.Ops(), error), "a first install");
		Check(d.files["pack/MRCustom_P.ucas"] == "new" && d.files.size() == 4, "the four files are in place, nothing else");
	}
	{
		Disk d;
		Built(d, "new");
		for (const char* ext : { "pak", "utoc", "ucas", "json" }) {
			d.files[std::string("pack/MRCustom_P.") + ext] = "old";
			d.files[std::string("pack/MRCustom_P.") + ext + ".bak"] = "older";
		}
		Check(InstallPack("out", "pack", "MRCustom_P", d.Ops(), error), "a replace");
		Check(d.files["pack/MRCustom_P.utoc"] == "new" && d.files["pack/MRCustom_P.utoc.bak"] == "old" && d.files.size() == 8,
			"the previous set is the .bak, the one before is gone");
	}
	{
		Disk d;
		Built(d, "new");
		d.files.erase("out/MRCustom_P.json");
		d.files["pack/MRCustom_P.pak"] = "old";
		Check(!InstallPack("out", "pack", "MRCustom_P", d.Ops(), error) && error.find("incomplete") != std::string::npos, "an incomplete build is refused");
		Check(d.files["pack/MRCustom_P.pak"] == "old" && !d.files.count("pack/MRCustom_P.pak.bak"), "and nothing moved");
	}
	{
		Disk d;
		Built(d, "new");
		for (const char* ext : { "pak", "utoc", "ucas", "json" }) d.files[std::string("pack/MRCustom_P.") + ext] = "old";
		d.failAt = 6;   // 4 backups, then the third new file fails
		Check(!InstallPack("out", "pack", "MRCustom_P", d.Ops(), error) && error.find("put back") != std::string::npos, "a replace failing half-way");
		bool restored = true;
		for (const char* ext : { "pak", "utoc", "ucas", "json" }) restored = restored && d.files[std::string("pack/MRCustom_P.") + ext] == "old";
		Check(restored, "the previous set is back in place");
	}

	Check(LayoutEnabled("") && LayoutEnabled("[Layout]\nbLayoutPack=1\n") && LayoutEnabled("[Style]\nbLayoutPack=0\n"), "sizes on by default");
	Check(!LayoutEnabled("[Layout]\nbLayoutPack = 0 ; no sizes pack\n") && !LayoutEnabled("\xEF\xBB\xBF[Layout]\r\nblayoutpack=0\r\n"),
		"bLayoutPack=0 turns them off (inline comment, key case, BOM)");
	Check(LayoutEnabled("[layout]\nbLayoutPack=0\n"), "section names are case-sensitive, as configparser reads them");

	json::Value colour;
	json::Parse(R"({"pack": "MRCustom_P", "textures": {"a": {}, "b": {}}, "skipped": [{"asset": "Art/X/T_Bad", "reason": "stock differs"}]})", colour, error);
	json::Value layout;
	json::Parse(R"({"pack": "MRLayout_P", "textures": {"c": {}}, "skipped": []})", layout, error);
	const auto text = BuildSummary(colour, &layout, "D:\\Plugins\\MenusRevamped\\CustomPack");
	Check(text.starts_with("MRCustom_P built: 2 asset(s) baked, 1 skipped.\nMRLayout_P built: 1 package(s) with the sizes, 0 skipped.\n"
						   "Installed in D:\\Plugins\\MenusRevamped\\CustomPack."),
		"summary: counts per pack, then where");
	Check(text.ends_with("Skipped (the game's stock look shows for these):\n  T_Bad: stock differs"), "summary: the skipped assets by name");

	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
