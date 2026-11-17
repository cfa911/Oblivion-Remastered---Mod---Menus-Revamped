// Preset rework (owner 2026-11-17, decision 48 extension): uPreset 0 Classic, 1 Dark, 2 Custom. Two folders, two names:
// the zip ships Plugins/MenusRevamped/DarkPack/MRDark_P.* (default colours); the Pack Builder writes
// Plugins/MenusRevamped/CustomPack/MRCustom_P.* (not shipped). At start: Classic removes both colour packs; Dark installs
// MRDark_P; Custom installs MRCustom_P when the player built a valid one, otherwise MRDark_P. Exactly one colour pack is in
// ~mods; the sizes pack MRLayout_P only when no colour pack is (a game package comes from one pak only).
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include "../src/core/rules/DarkPackSwitch.h"
namespace fs = std::filesystem;
using darkpackswitch::Outcome;
#if __has_include("../src/core/rules/PresetPacks.h")
#include "../src/core/rules/PresetPacks.h"
#else
// Pre-fix behaviour: no preset pack choice exists; nothing installs MRDark_P from DarkPack.
namespace presetpacks {
enum class Colours { None, Dark, Custom };
inline Colours Choose(int, bool, bool) { return Colours::None; }
struct Outcomes { Outcome dark = Outcome::Failed; Outcome custom = Outcome::Failed; Outcome layout = Outcome::Failed; };
inline Outcomes Switch(const fs::path&, const fs::path&, const fs::path&, Colours, bool, bool) { return {}; }
inline bool Present(const fs::path&, const wchar_t*) { return false; }
}
#endif
using presetpacks::Colours;
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", why); } }
void Write(const fs::path& file, const std::string& text) { fs::create_directories(file.parent_path()); std::ofstream(file, std::ios::binary) << text; }
std::string Read(const fs::path& file) { std::ifstream in(file, std::ios::binary); return { std::istreambuf_iterator<char>(in), {} }; }
const wchar_t* kExt[]{ L".pak", L".utoc", L".ucas" };
bool AllIn(const fs::path& dir, const wchar_t* pack) { for (auto* e : kExt) if (!fs::exists(dir / (std::wstring(pack) + e))) return false; return true; }
bool NoneIn(const fs::path& dir, const wchar_t* pack) { for (auto* e : kExt) if (fs::exists(dir / (std::wstring(pack) + e))) return false; return true; }

int main()
{
	// the choice: preset (0 Classic, 1 Dark, 2 Custom), the player's pack allowed, the shipped Dark pack allowed
	Check(presetpacks::Choose(0, true, true) == Colours::None, "Classic uses no colour pack");
	Check(presetpacks::Choose(1, true, true) == Colours::Dark, "Dark uses the shipped Dark pack even when the player built one");
	Check(presetpacks::Choose(1, false, true) == Colours::Dark, "Dark uses the shipped Dark pack");
	Check(presetpacks::Choose(2, true, true) == Colours::Custom, "Custom uses the player's pack when it is built and allowed");
	Check(presetpacks::Choose(2, false, true) == Colours::Dark, "Custom without a built pack shows Dark");
	Check(presetpacks::Choose(1, true, false) == Colours::None, "Dark with an unusable shipped pack installs nothing");
	Check(presetpacks::Choose(2, false, false) == Colours::None, "Custom with neither pack usable installs nothing");

	const auto root = fs::temp_directory_path() / "mr-preset-packs-test";
	fs::remove_all(root);
	const auto darkSource = root / "Plugins/MenusRevamped/DarkPack";
	const auto customSource = root / "Plugins/MenusRevamped/CustomPack";
	const auto mods = root / "Content/Paks/~mods";
	Check(!presetpacks::Present(customSource, L"MRCustom_P"), "no player pack: not present");
	for (auto* e : kExt) {
		Write(darkSource / (std::wstring(L"MRDark_P") + e), "dark");
		Write(customSource / (std::wstring(L"MRCustom_P") + e), "custom");
		Write(customSource / (std::wstring(L"MRLayout_P") + e), "sizes");
	}
	Check(presetpacks::Present(customSource, L"MRCustom_P") && presetpacks::Present(darkSource, L"MRDark_P"), "all three files: present");
	Write(mods / L"OtherMod_P.pak", "other");

	// Dark: MRDark_P from DarkPack, nothing else of ours
	auto r = presetpacks::Switch(darkSource, customSource, mods, Colours::Dark, true, true);
	Check(darkpackswitch::Active(r.dark) && AllIn(mods, L"MRDark_P"), "Dark installs MRDark_P from DarkPack");
	Check(Read(mods / L"MRDark_P.pak") == "dark", "the Dark pack in ~mods is the shipped one");
	Check(NoneIn(mods, L"MRCustom_P") && NoneIn(mods, L"MRLayout_P"), "Dark leaves neither MRCustom_P nor MRLayout_P in ~mods");

	// Custom with the player's pack: MRCustom_P replaces MRDark_P
	r = presetpacks::Switch(darkSource, customSource, mods, Colours::Custom, true, true);
	Check(darkpackswitch::Active(r.custom) && AllIn(mods, L"MRCustom_P"), "Custom installs MRCustom_P from CustomPack");
	Check(NoneIn(mods, L"MRDark_P") && NoneIn(mods, L"MRLayout_P"), "Custom removes MRDark_P and never adds MRLayout_P");

	// back to Dark
	r = presetpacks::Switch(darkSource, customSource, mods, Colours::Dark, true, true);
	Check(AllIn(mods, L"MRDark_P") && NoneIn(mods, L"MRCustom_P"), "switching to Dark swaps MRCustom_P for MRDark_P");

	// Classic: both colour packs go; the sizes pack comes when it is on and allowed
	r = presetpacks::Switch(darkSource, customSource, mods, Colours::None, true, true);
	Check(NoneIn(mods, L"MRDark_P") && NoneIn(mods, L"MRCustom_P"), "Classic removes both colour packs");
	Check(darkpackswitch::Active(r.layout) && AllIn(mods, L"MRLayout_P"), "Classic installs MRLayout_P (bLayoutPack=1, allowed)");
	r = presetpacks::Switch(darkSource, customSource, mods, Colours::None, false, true);
	Check(NoneIn(mods, L"MRLayout_P"), "Classic with bLayoutPack=0 removes MRLayout_P");
	Check(fs::exists(mods / L"OtherMod_P.pak"), "another mod's pack is left alone");

	// the shipped Dark pack missing a file: nothing of it in ~mods
	fs::remove(darkSource / L"MRDark_P.ucas");
	Check(!presetpacks::Present(darkSource, L"MRDark_P"), "two of three files: not present");
	r = presetpacks::Switch(darkSource, customSource, mods, Colours::Dark, false, false);
	Check(r.dark == Outcome::Incomplete && NoneIn(mods, L"MRDark_P"), "an incomplete Dark pack is not installed");

	fs::remove_all(root);
	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
