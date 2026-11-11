// Custom pack builder Phase 3 (owner 2026-11-11): at load the plugin reads MRCustom_P.json (written by tools/make_dark_pack.py)
// and decides (owner decision 6, log only): colours changed since the build = install the last pack, one warning; game build
// differs = do not install, remove from ~mods, one warning. The pack's colours are the manifest "style" values (the baked
// elements and tab greys); the run-time [Skin] tints are not baked, so changing them never makes the pack stale.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include "../src/core/DarkStyle.h"
#if __has_include("../src/core/rules/CustomPackManifest.h")
#include "../src/core/rules/CustomPackManifest.h"
#else
// Pre-fix behaviour: no manifest check; the pack is always installed.
#include <vector>
namespace custompack {
struct Manifest { bool ok = false; std::string gameBuild; std::vector<std::pair<std::string, std::vector<float>>> style; };
inline Manifest ParseManifest(std::string_view) { return {}; }
enum class Verdict { Current, ColoursChanged, WrongBuild, Unreadable };
struct Decision { Verdict verdict = Verdict::Current; std::vector<std::string> changed; bool install = true; };
inline Decision Decide(const Manifest&, std::string_view, const darkstyle::Style&) { return {}; }
inline bool Known(std::string_view) { return false; }
}
#endif
namespace fs = std::filesystem;
using custompack::Verdict;
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", why); } }
std::string ReadFile(const fs::path& f) { std::ifstream in(f, std::ios::binary); return { std::istreambuf_iterator<char>(in), {} }; }

const char* kSample = R"({
  "pack": "MRCustom_P",
  "game_build": "1.512.105.0",
  "built_by": "tools/make_dark_pack.py",
  "style": {
    "detailsPage": 0.01,
    "panel": [
      41.0,
      41.0,
      41.0
    ],
    "glyph": 242.0,
    "effectBox": [66.0, 99.0, 153.0]
  },
  "textures": { "UI/x": { "recipe": "flat:panel", "style": ["panel"], "stock_uexp_sha256": "ab" } },
  "files": { "MRCustom_P.pak": "cd" }
})";

int main()
{
	const auto& d = darkstyle::kDefaults;
	const auto m = custompack::ParseManifest(kSample);
	Check(m.ok && m.gameBuild == "1.512.105.0", "the manifest's game_build is read");
	Check(m.style.size() == 4, "the manifest's style values are read (numbers and r, g, b lists), not the textures' style lists");

	auto r = custompack::Decide(m, "1.512.105.0", d);
	Check(r.verdict == Verdict::Current && r.install && r.changed.empty(), "same build and colours: the pack is current and installed");

	auto s = d;
	s.panel = { 50, 41, 41 };
	r = custompack::Decide(m, "1.512.105.0", s);
	Check(r.verdict == Verdict::ColoursChanged && r.install, "a changed baked colour: the last pack is still installed (stale)");
	Check(r.changed.size() == 1 && r.changed[0] == "panel", "the stale decision names the changed colour");
	s = d; s.tab.glyph = 200;
	Check(custompack::Decide(m, "1.512.105.0", s).verdict == Verdict::ColoursChanged, "a changed tab grey makes the pack stale");
	s = d; s.detailsPage = 0.5f;
	Check(custompack::Decide(m, "1.512.105.0", s).verdict == Verdict::ColoursChanged, "a changed detailsPage makes the pack stale");
	s = d; s.skin.rule = { 0.1f, 0.1f, 0.1f, 1 }; s.rowText = { 1, 2, 3 };
	Check(custompack::Decide(m, "1.512.105.0", s).verdict == Verdict::Current,
		"run-time tints and colours the pack does not bake (not in its style) never make it stale");

	const auto laurelPack = custompack::ParseManifest(R"({"game_build":"1.512.105.0","style":{"pattern":[150,150,150],"Skin.pattern":[1,0,0,0.5]}})");
	Check(custompack::HasBakedLaurelTint(laurelPack) && !custompack::HasBakedLaurelTint(m),
		"only a new pack with the baked tint disables the runtime tint");
	s = d; s.skin.pattern = { 1, 0, 0, 0.5f };
	Check(custompack::Decide(laurelPack, "1.512.105.0", s).verdict == Verdict::Current, "matching laurel tint is current");
	s.skin.pattern[3] = 1;
	r = custompack::Decide(laurelPack, "1.512.105.0", s);
	Check(r.verdict == Verdict::ColoursChanged && r.install && r.changed == std::vector<std::string>{ "Skin.pattern" },
		"a changed laurel tint keeps the pack and names the Skin key separately from the base colour");
	Check(custompack::Decide(m, "1.512.105.0", s).verdict == Verdict::Current, "Phase 4 manifests stay compatible");

	// Phase 6 (owner decision 14): the alchemy recipe bakes alchemyMetal and alchemyStone, so a changed key makes the pack stale
	const auto alchemyPack = custompack::ParseManifest(
		R"({"game_build":"1.512.105.0","style":{"alchemyMetal":[56,83,118],"alchemyStone":[22,25,31]}})");
	Check(custompack::Known("alchemyMetal") && custompack::Known("alchemyStone"), "alchemyMetal and alchemyStone are baked colours the plugin reads");
	Check(custompack::Decide(alchemyPack, "1.512.105.0", d).verdict == Verdict::Current, "the default Alchemy colours match a default pack");
	r = custompack::Decide(alchemyPack, "1.512.105.0", customcolours::Parse("[Colours]\nalchemyMetal = 200, 40, 40\n").style);
	Check(r.verdict == Verdict::ColoursChanged && r.install && r.changed == std::vector<std::string>{ "alchemyMetal" },
		"a changed alchemyMetal in CustomColours.ini makes the pack stale and is named");
	r = custompack::Decide(alchemyPack, "1.512.105.0", customcolours::Parse("[Colours]\nalchemyStone = 90, 90, 90\n").style);
	Check(r.verdict == Verdict::ColoursChanged && r.changed == std::vector<std::string>{ "alchemyStone" },
		"a changed alchemyStone makes the pack stale and is named");
	const auto rendered = customcolours::Render(d);
	Check(rendered.find("alchemyMetal = 56, 83, 118") != std::string::npos && rendered.find("alchemyStone = 22, 25, 31") != std::string::npos,
		"the plugin's default CustomColours.ini lists both Alchemy keys");

	s = d; s.panel = { 50, 41, 41 };
	r = custompack::Decide(m, "1.600.1.0", s);
	Check(r.verdict == Verdict::WrongBuild && !r.install, "another game build: the pack is not installed (wins over stale colours)");

	for (const char* bad : { "", "not json", "{\"pack\": \"MRCustom_P\"}", "{\"game_build\": \"1.512.105.0\", \"style\": {\"panel\": [1, 2}" }) {
		r = custompack::Decide(custompack::ParseManifest(bad), "1.512.105.0", d);
		Check(r.verdict == Verdict::Unreadable && !r.install, "a missing or broken manifest: the pack is not installed (cannot check its build)");
	}

	// the shipped development pack's manifest: every style key maps onto a plugin value, and it matches the defaults
	const auto repo = fs::path(__FILE__).parent_path().parent_path();
	const auto shipped = custompack::ParseManifest(ReadFile(repo / "assets/CustomPack/MRCustom_P.json"));
	Check(shipped.ok && shipped.gameBuild == "1.512.105.0", "assets/CustomPack/MRCustom_P.json is read (game build 1.512.105.0)");
	bool known = !shipped.style.empty();
	for (const auto& [key, value] : shipped.style) {
		if (!custompack::Known(key)) { known = false; std::printf("  unknown manifest key %s\n", key.c_str()); }
	}
	Check(known, "every style key of the shipped manifest is a colour the plugin reads");
	Check(shipped.style.size() >= 29, "the shipped manifest holds the baked colours (29 today)");
	Check(custompack::Decide(shipped, "1.512.105.0", d).verdict == Verdict::Current, "the shipped pack is current for the default colours");

	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
