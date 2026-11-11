// Custom pack builder Phase 3 (owner 2026-11-11): uPreset=2 copies MRCustom_P.* (all three or none) from
// Plugins/MenusRevamped/CustomPack into Content/Paks/~mods and removes an old MRDark_P.* there (upgrade path); the other presets
// remove both. A pack built for another game build is not installed and is removed from ~mods (owner decision 6).
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include "../src/core/rules/DarkPackSwitch.h"
namespace fs = std::filesystem;
using darkpackswitch::Outcome;
#if !__has_include("../src/core/rules/CustomPackManifest.h")
// Pre-fix behaviour: only the MRDark_P switch exists; nothing handles MRCustom_P.
namespace darkpackswitch {
inline constexpr Outcome WrongBuildStub = Outcome::Failed;
inline Outcome SwitchCustom(const fs::path&, const fs::path&, bool, bool) { return Outcome::Failed; }
}
#define WRONG_BUILD darkpackswitch::WrongBuildStub
#else
#define WRONG_BUILD Outcome::WrongBuild
#endif
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", why); } }
void Write(const fs::path& file, const std::string& text) { fs::create_directories(file.parent_path()); std::ofstream(file, std::ios::binary) << text; }
std::string Read(const fs::path& file) { std::ifstream in(file, std::ios::binary); return { std::istreambuf_iterator<char>(in), {} }; }
const wchar_t* kExt[]{ L".pak", L".utoc", L".ucas" };
bool AllIn(const fs::path& dir, const wchar_t* pack) { for (auto* e : kExt) if (!fs::exists(dir / (std::wstring(pack) + e))) return false; return true; }
bool NoneIn(const fs::path& dir, const wchar_t* pack) { for (auto* e : kExt) if (fs::exists(dir / (std::wstring(pack) + e))) return false; return true; }
void OldDark(const fs::path& mods) { for (auto* e : kExt) Write(mods / (std::wstring(L"MRDark_P") + e), "old"); }

int main()
{
	const auto root = fs::temp_directory_path() / "mr-custom-pack-switch-test";
	fs::remove_all(root);
	const auto source = root / "Plugins/MenusRevamped/CustomPack";
	const auto mods = root / "Content/Paks/~mods";
	for (auto* e : kExt) Write(source / (std::wstring(L"MRCustom_P") + e), std::string("c1") + char('a' + (e[1] - L'a')));

	// upgrade from the Dark pack: MRDark_P in ~mods, Custom preset
	OldDark(mods);
	Write(mods / L"OtherMod_P.pak", "other");
	auto r = darkpackswitch::SwitchCustom(source, mods, true, true);
	Check(r == Outcome::Installed && AllIn(mods, L"MRCustom_P"), "Custom copies all three MRCustom_P files into ~mods");
	Check(NoneIn(mods, L"MRDark_P"), "Custom removes an old MRDark_P from ~mods (upgrade path)");
	Check(Read(mods / L"MRCustom_P.ucas") == Read(source / L"MRCustom_P.ucas"), "the copy matches the plugin's pack");
	Check(fs::exists(mods / L"OtherMod_P.pak"), "another mod's pack is left alone");
	Check(darkpackswitch::Active(r), "an installed Custom pack is active");

	r = darkpackswitch::SwitchCustom(source, mods, true, true);
	Check(r == Outcome::UpToDate && darkpackswitch::Active(r), "a second Custom start copies nothing and stays active");

	// a clean install (no ~mods yet)
	fs::remove_all(mods);
	r = darkpackswitch::SwitchCustom(source, mods, true, true);
	Check(r == Outcome::Installed && AllIn(mods, L"MRCustom_P"), "Custom on a clean install creates ~mods and copies the pack");
	Write(mods / L"OtherMod_P.pak", "other");

	// game build differs from the manifest: not installed, removed from ~mods
	OldDark(mods);
	r = darkpackswitch::SwitchCustom(source, mods, true, false);
	Check(r == WRONG_BUILD && NoneIn(mods, L"MRCustom_P"), "a pack for another game build is removed from ~mods, not installed");
	Check(NoneIn(mods, L"MRDark_P"), "the wrong-build case still removes an old MRDark_P");
	Check(!darkpackswitch::Active(r), "a wrong-build pack is not active (the run-time skin keeps working)");

	// another preset removes both
	darkpackswitch::SwitchCustom(source, mods, true, true);
	OldDark(mods);
	r = darkpackswitch::SwitchCustom(source, mods, false, true);
	Check(r == Outcome::Removed && NoneIn(mods, L"MRCustom_P") && NoneIn(mods, L"MRDark_P"), "Classic/Light remove MRCustom_P and MRDark_P");
	r = darkpackswitch::SwitchCustom(source, mods, false, true);
	Check(r == Outcome::Absent, "Classic with neither pack installed reports nothing to remove");
	OldDark(mods);
	r = darkpackswitch::SwitchCustom(source, mods, false, true);
	Check(r == Outcome::Removed && NoneIn(mods, L"MRDark_P"), "Classic removes a lone old MRDark_P (counts as removed)");

	// half-installed plugin pack
	fs::remove(source / L"MRCustom_P.utoc");
	r = darkpackswitch::SwitchCustom(source, mods, true, true);
	Check(r == Outcome::Incomplete && NoneIn(mods, L"MRCustom_P"), "a missing MRCustom_P file in the plugin folder leaves nothing in ~mods");
	Check(fs::exists(mods / L"OtherMod_P.pak"), "other mods stay untouched throughout");

	fs::remove_all(root);
	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
