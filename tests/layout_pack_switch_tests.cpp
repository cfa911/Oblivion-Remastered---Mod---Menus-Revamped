// Custom pack builder Step B (owner 2026-11-12, decision 46): the plugin installs exactly one of the two built packs.
// MRCustom_P (colours + sizes) for uPreset=2 when it is present and allowed; otherwise MRLayout_P (sizes only) when
// [Layout] bLayoutPack=1 and its manifest is for this game build. Never both: a game package can come from one pak only.
// The manifests: MRLayout_P.json holds "game_build" and "sizes"; MRCustom_P.json holds "sizes" when the colour pack carries them.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include "../src/core/rules/DarkPackSwitch.h"
namespace fs = std::filesystem;
using darkpackswitch::Outcome;
#if __has_include("../src/core/rules/LayoutPack.h")
#include "../src/core/rules/LayoutPack.h"
#else
// Pre-fix behaviour: nothing knows MRLayout_P; only the colour pack switch exists.
namespace layoutpack {
struct Outcomes { Outcome custom = Outcome::Failed; Outcome layout = Outcome::Failed; };
inline Outcomes Switch(const fs::path& s, const fs::path& m, bool c, bool ci, bool, bool)
{
	return { darkpackswitch::SwitchCustom(s, m, c, ci), Outcome::Failed };
}
inline bool LayoutManifestOk(std::string_view, std::string_view) { return false; }
inline bool HasSizes(std::string_view) { return false; }
}
#endif
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", why); } }
void Write(const fs::path& file, const std::string& text) { fs::create_directories(file.parent_path()); std::ofstream(file, std::ios::binary) << text; }
const wchar_t* kExt[]{ L".pak", L".utoc", L".ucas" };
bool AllIn(const fs::path& dir, const wchar_t* pack) { for (auto* e : kExt) if (!fs::exists(dir / (std::wstring(pack) + e))) return false; return true; }
bool NoneIn(const fs::path& dir, const wchar_t* pack) { for (auto* e : kExt) if (fs::exists(dir / (std::wstring(pack) + e))) return false; return true; }

int main()
{
	const auto root = fs::temp_directory_path() / "mr-layout-pack-switch-test";
	fs::remove_all(root);
	const auto source = root / "Plugins/MenusRevamped/CustomPack";
	const auto mods = root / "Content/Paks/~mods";
	for (auto* e : kExt) {
		Write(source / (std::wstring(L"MRCustom_P") + e), "colours");
		Write(source / (std::wstring(L"MRLayout_P") + e), "sizes");
	}
	Write(mods / L"OtherMod_P.pak", "other");

	// Custom: the colour pack (it carries the sizes), never the layout pack next to it
	auto r = layoutpack::Switch(source, mods, true, true, true, true);
	Check(darkpackswitch::Active(r.custom) && AllIn(mods, L"MRCustom_P"), "Custom installs MRCustom_P");
	Check(!darkpackswitch::Active(r.layout) && NoneIn(mods, L"MRLayout_P"), "Custom never installs MRLayout_P next to it");

	// Classic or Light: the layout pack only; the colour pack is removed
	r = layoutpack::Switch(source, mods, false, true, true, true);
	Check(darkpackswitch::Active(r.layout) && AllIn(mods, L"MRLayout_P"), "another preset installs MRLayout_P");
	Check(NoneIn(mods, L"MRCustom_P"), "another preset removes MRCustom_P");
	Check(fs::exists(mods / L"OtherMod_P.pak"), "another mod's pack is left alone");

	// back to Custom: the layout pack goes, the colour pack comes
	r = layoutpack::Switch(source, mods, true, true, true, true);
	Check(AllIn(mods, L"MRCustom_P") && NoneIn(mods, L"MRLayout_P"), "switching to Custom swaps MRLayout_P for MRCustom_P");

	// Vanilla colours (the builder moved MRCustom_P away): Custom falls back to the sizes pack
	for (auto* e : kExt) fs::remove(source / (std::wstring(L"MRCustom_P") + e));
	r = layoutpack::Switch(source, mods, true, true, true, true);
	Check(NoneIn(mods, L"MRCustom_P") && AllIn(mods, L"MRLayout_P") && darkpackswitch::Active(r.layout),
		"Custom without a colour pack (Vanilla colours) installs MRLayout_P: the sizes stay, the colours are the game's");
	for (auto* e : kExt) Write(source / (std::wstring(L"MRCustom_P") + e), "colours");

	// a colour pack for another game build: not installed; the sizes pack takes over
	r = layoutpack::Switch(source, mods, true, false, true, true);
	Check(NoneIn(mods, L"MRCustom_P") && AllIn(mods, L"MRLayout_P"), "a colour pack that is not allowed falls back to MRLayout_P");

	// bLayoutPack=0 or a layout pack for another build: removed
	r = layoutpack::Switch(source, mods, false, true, false, true);
	Check(NoneIn(mods, L"MRLayout_P") && !darkpackswitch::Active(r.layout), "bLayoutPack=0 removes MRLayout_P");
	layoutpack::Switch(source, mods, false, true, true, true);
	r = layoutpack::Switch(source, mods, false, true, true, false);
	Check(NoneIn(mods, L"MRLayout_P"), "a layout pack for another game build is removed");

	// manifests
	const std::string lay = "{\"pack\": \"MRLayout_P\", \"game_build\": \"1.512.105.0\", \"sizes\": {\"inventoryRowHeight\": 36.0}, \"textures\": {}}";
	Check(layoutpack::LayoutManifestOk(lay, "1.512.105.0"), "a layout manifest for this game build allows MRLayout_P");
	Check(!layoutpack::LayoutManifestOk(lay, "1.600.1.0"), "a layout manifest for another game build does not");
	Check(!layoutpack::LayoutManifestOk("{\"game_build\": \"1.512.105.0\"}", "1.512.105.0"), "a manifest without sizes does not");
	Check(!layoutpack::LayoutManifestOk("", "1.512.105.0"), "a missing manifest does not");
	Check(layoutpack::HasSizes("{\"game_build\": \"1.512.105.0\", \"style\": {}, \"sizes\": {\"a\": 1}}"), "a colour pack with sizes says so");
	Check(!layoutpack::HasSizes("{\"game_build\": \"1.512.105.0\", \"style\": {}}"), "an older colour pack has no sizes");

	fs::remove_all(root);
	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
