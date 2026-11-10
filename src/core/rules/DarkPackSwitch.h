#pragma once
// The Dark UI pack switch (tests/dark_pack_switch_tests.cpp): at plugin load, before the engine mounts Content/Paks/~mods,
// Dark copies the plugin's pack (Plugins/MenusRevamped/DarkPack/MRDark_P.pak/.utoc/.ucas) into ~mods and the other presets
// remove it. All or nothing: the engine never sees part of a pack. Another mod's files are never touched.
#include <filesystem>
#include <string>
#include <system_error>

namespace darkpackswitch
{
	enum class Outcome
	{
		Installed,    // Dark: copied (new or changed)
		UpToDate,     // Dark: the copy in ~mods already matches
		Removed,      // not Dark: the pack was in ~mods and is gone
		Absent,       // not Dark: nothing to remove
		Incomplete,   // Dark, but the plugin's pack is missing a file: nothing left in ~mods
		Failed,       // a copy or remove failed: nothing of ours left in ~mods (as far as the OS allowed)
	};

	inline bool Active(Outcome outcome) { return outcome == Outcome::Installed || outcome == Outcome::UpToDate; }

	inline constexpr const wchar_t* kExtensions[]{ L".pak", L".utoc", L".ucas" };
	inline constexpr const wchar_t* kTempSuffix = L".mrtmp";

	// Removes the pack and any temp copies from ~mods; true when none of them is left.
	inline bool RemoveAll(const std::filesystem::path& mods, const std::wstring& name)
	{
		namespace fs = std::filesystem;
		bool clean = true;
		for (const auto* extension : kExtensions) {
			for (const auto& file : { mods / (name + extension), mods / (name + extension + kTempSuffix) }) {
				std::error_code error;
				fs::remove(file, error);
				if (fs::exists(file, error)) clean = false;
			}
		}
		return clean;
	}

	inline Outcome Switch(const std::filesystem::path& source, const std::filesystem::path& mods, bool dark,
		const wchar_t* packName = L"MRDark_P")
	{
		namespace fs = std::filesystem;
		const std::wstring name = packName;
		std::error_code error;
		if (!dark) {
			bool present = false;
			for (const auto* extension : kExtensions) present = present || fs::exists(mods / (name + extension), error);
			return RemoveAll(mods, name) ? (present ? Outcome::Removed : Outcome::Absent) : Outcome::Failed;
		}
		for (const auto* extension : kExtensions) {
			if (!fs::is_regular_file(source / (name + extension), error)) {
				RemoveAll(mods, name);
				return Outcome::Incomplete;
			}
		}
		fs::create_directories(mods, error);   // a clean install has no ~mods folder
		bool copied = false;
		for (const auto* extension : kExtensions) {
			const auto from = source / (name + extension);
			const auto target = mods / (name + extension);
			const auto temp = mods / (name + extension + kTempSuffix);
			fs::remove(temp, error);   // left by an interrupted start
			error.clear();
			// the copy carries the source's date: any other date or size means the plugin's pack changed (a BC3 rebuild keeps the size)
			const auto fromTime = fs::last_write_time(from, error);
			if (error) { RemoveAll(mods, name); return Outcome::Failed; }
			std::error_code probe;
			if (fs::exists(target, probe) && fs::file_size(target, probe) == fs::file_size(from, probe) && !probe &&
				fs::last_write_time(target, probe) == fromTime && !probe)
				continue;
			fs::copy_file(from, temp, fs::copy_options::overwrite_existing, error);
			if (!error) fs::last_write_time(temp, fromTime, error);
			if (!error) fs::rename(temp, target, error);   // replaces an older copy
			if (error) { RemoveAll(mods, name); return Outcome::Failed; }
			copied = true;
		}
		return copied ? Outcome::Installed : Outcome::UpToDate;
	}
}
