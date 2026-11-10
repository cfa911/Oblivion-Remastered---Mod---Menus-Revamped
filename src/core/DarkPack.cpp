// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DarkPack.h"
#include "core/Config.h"

namespace darkpack
{
	namespace
	{
		bool g_active = false;
		constexpr const wchar_t* kName = L"MRDark_P";
		constexpr const wchar_t* kExtensions[]{ L".pak", L".utoc", L".ucas" };
	}

	void Apply()
	{
		namespace fs = std::filesystem;
		const auto plugins = mrconfig::PluginFolder();                    // ...\Binaries\Win64\OBSE\Plugins
		const auto source = plugins / L"MenusRevamped" / L"DarkPack";
		const auto mods = plugins.parent_path().parent_path().parent_path().parent_path() / L"Content" / L"Paks" / L"~mods";
		const bool dark = mrconfig::Get().stylePreset == uistyle::Preset::Dark;
		std::error_code error;
		bool complete = dark;
		for (const auto* extension : kExtensions) {
			const auto file = std::wstring(kName) + extension;
			const auto target = mods / file;
			if (!dark) {   // Classic and Light: the game's own textures
				if (fs::exists(target, error)) {
					fs::remove(target, error);
					if (error) logger::warn("Menus Revamped: could not remove {} ({})", target.filename().string(), error.message());
				}
				continue;
			}
			const auto from = source / file;
			if (!fs::exists(from, error)) {
				logger::warn("Menus Revamped: Dark pack file missing: {}", from.string());
				complete = false;
				continue;
			}
			const bool same = fs::exists(target, error) && fs::file_size(target, error) == fs::file_size(from, error) &&
				fs::last_write_time(target, error) >= fs::last_write_time(from, error);
			if (!same) fs::copy_file(from, target, fs::copy_options::overwrite_existing, error);
			if (error) {
				logger::warn("Menus Revamped: could not copy {} to ~mods ({})", from.filename().string(), error.message());
				complete = false;
			}
		}
		g_active = complete;
		logger::info("Menus Revamped: Dark pack {}", dark ? (complete ? "in ~mods" : "incomplete") : "removed (not Dark)");
	}

	bool Active() { return g_active; }
}
