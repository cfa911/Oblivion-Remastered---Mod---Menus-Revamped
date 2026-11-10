// Dark UI pack phase 1 (owner 2026-11-10): the start-up switch copies MRDark_P into Content/Paks/~mods in Dark and removes it
// otherwise. A release must survive a clean install (no ~mods folder yet), a missing or half-installed pack (never leave part of
// a pack for the engine to mount) and a pack update with the same file size (BC3 rebuilds keep the size).
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#if __has_include("../src/core/rules/DarkPackSwitch.h")
#include "../src/core/rules/DarkPackSwitch.h"
#else
// Pre-fix behaviour: no tested switch exists, nothing is ever installed.
namespace darkpackswitch {
enum class Outcome { Installed, UpToDate, Removed, Absent, Incomplete, Failed };
inline Outcome Switch(const std::filesystem::path&, const std::filesystem::path&, bool, const wchar_t* = L"MRDark_P") { return Outcome::Failed; }
inline bool Active(Outcome) { return false; }
}
#endif
namespace fs = std::filesystem;
using darkpackswitch::Outcome;
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", why); } }

void Write(const fs::path& file, const std::string& text) { fs::create_directories(file.parent_path()); std::ofstream(file, std::ios::binary) << text; }
std::string Read(const fs::path& file) { std::ifstream in(file, std::ios::binary); return { std::istreambuf_iterator<char>(in), {} }; }
const wchar_t* kExt[]{ L".pak", L".utoc", L".ucas" };
bool AllIn(const fs::path& dir) { for (auto* e : kExt) if (!fs::exists(dir / (std::wstring(L"MRDark_P") + e))) return false; return true; }
bool NoneIn(const fs::path& dir) { for (auto* e : kExt) if (fs::exists(dir / (std::wstring(L"MRDark_P") + e))) return false; return true; }

int main()
{
	const auto root = fs::temp_directory_path() / "mr-dark-pack-switch-test";
	fs::remove_all(root);
	const auto source = root / "Plugins/MenusRevamped/DarkPack";
	const auto mods = root / "Content/Paks/~mods";
	for (auto* e : kExt) Write(source / (std::wstring(L"MRDark_P") + e), std::string("v1") + char('a' + (e[1] - L'a')));

	// clean install: ~mods does not exist yet
	auto r = darkpackswitch::Switch(source, mods, true);
	Check(r == Outcome::Installed && AllIn(mods), "Dark on a clean install creates ~mods and copies all three pack files");
	Check(Read(mods / L"MRDark_P.ucas") == Read(source / L"MRDark_P.ucas"), "the copied files match the plugin's pack");
	Check(darkpackswitch::Active(r), "an installed pack is active");

	r = darkpackswitch::Switch(source, mods, true);
	Check(r == Outcome::UpToDate && darkpackswitch::Active(r), "a second Dark start with the same pack copies nothing and stays active");

	// a pack update of the same size (a BC3 rebuild) must be copied again
	Write(source / L"MRDark_P.ucas", "v2c");
	std::error_code ec;
	const auto installed = fs::last_write_time(mods / L"MRDark_P.ucas", ec);
	if (!ec) fs::last_write_time(source / L"MRDark_P.ucas", installed - std::chrono::hours(1), ec);
	r = darkpackswitch::Switch(source, mods, true);
	Check(r == Outcome::Installed && Read(mods / L"MRDark_P.ucas") == "v2c", "an updated pack file of the same size is copied again (even with an older date)");

	// a leftover temp file from an interrupted copy is cleared
	Write(mods / L"MRDark_P.ucas.mrtmp", "partial");
	darkpackswitch::Switch(source, mods, true);
	Check(!fs::exists(mods / L"MRDark_P.ucas.mrtmp"), "a leftover temp copy is removed");

	// another mod's files are never touched
	Write(mods / L"OtherMod_P.pak", "other");
	r = darkpackswitch::Switch(source, mods, false);
	Check(r == Outcome::Removed && NoneIn(mods), "Classic/Light removes all three pack files");
	Check(fs::exists(mods / L"OtherMod_P.pak"), "another mod's pack in ~mods is left alone");
	Check(!darkpackswitch::Active(r), "a removed pack is not active");
	r = darkpackswitch::Switch(source, mods, false);
	Check(r == Outcome::Absent, "Classic with no pack installed reports nothing to remove");

	// half-installed plugin pack: nothing may be left for the engine to mount
	darkpackswitch::Switch(source, mods, true);
	fs::remove(source / L"MRDark_P.utoc");
	r = darkpackswitch::Switch(source, mods, true);
	Check(r == Outcome::Incomplete && NoneIn(mods), "a missing pack file in the plugin folder removes the whole pack from ~mods");
	Check(!darkpackswitch::Active(r), "an incomplete pack is not active (the run-time skin keeps working)");
	Check(fs::exists(mods / L"OtherMod_P.pak"), "the incomplete case still leaves other mods alone");

	fs::remove_all(root);
	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
