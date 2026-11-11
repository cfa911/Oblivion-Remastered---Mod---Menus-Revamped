// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DarkPack.h"
#include "core/Config.h"
#include "core/rules/CustomColours.h"
#include "core/rules/CustomPackManifest.h"
#include "core/rules/DarkPackSwitch.h"
#include <fstream>
#include <iterator>

namespace darkpack
{
	namespace
	{
		bool g_active = false;

		const char* Describe(darkpackswitch::Outcome outcome)
		{
			switch (outcome) {
			case darkpackswitch::Outcome::Installed: return "copied into ~mods";
			case darkpackswitch::Outcome::UpToDate: return "already in ~mods";
			case darkpackswitch::Outcome::Removed: return "removed from ~mods (preset is not Custom)";
			case darkpackswitch::Outcome::Absent: return "not installed (preset is not Custom)";
			case darkpackswitch::Outcome::Incomplete: return "missing a file in Plugins/MenusRevamped/CustomPack: not installed";
			case darkpackswitch::Outcome::WrongBuild: return "removed from ~mods: its manifest does not allow it (see the line above)";
			default: return "could not be copied or removed (file in use or no permission): not installed";
			}
		}

		bool ReadText(const std::filesystem::path& file, std::string& out)
		{
			std::ifstream in(file, std::ios::binary);
			if (!in) return false;
			out.assign(std::istreambuf_iterator<char>(in), {});
			return true;
		}

		// CustomColours.ini over the defaults; writes the default file when it is missing (the zip ships none, so a mod update
		// never replaces the user's colours).
		void LoadColours(const std::filesystem::path& file)
		{
			std::string text;
			if (!ReadText(file, text)) {
				std::error_code error;
				std::filesystem::create_directories(file.parent_path(), error);
				std::ofstream(file, std::ios::binary) << customcolours::Render(darkstyle::kDefaults);
				logger::info("Menus Revamped: wrote the default colour file {}", file.string());
				return;
			}
			const auto parsed = customcolours::Parse(text);
			for (const auto& note : parsed.notes) {
				if (note.warning)
					logger::warn("Menus Revamped: CustomColours.ini {}", note.text);
				else
					logger::info("Menus Revamped: CustomColours.ini {}", note.text);
			}
			darkstyle::Set(parsed.style);
		}

		std::string Join(const std::vector<std::string>& names)
		{
			std::string out;
			for (const auto& n : names) out += (out.empty() ? "" : ", ") + n;
			return out;
		}
	}

	void Apply(std::string_view a_gameBuild)
	{
		const auto plugins = mrconfig::PluginFolder();   // ...\Binaries\Win64\OBSE\Plugins
		const auto folder = plugins / L"MenusRevamped";
		const auto source = folder / L"CustomPack";
		const auto mods = plugins.parent_path().parent_path().parent_path().parent_path() / L"Content" / L"Paks" / L"~mods";
		const bool custom = mrconfig::Get().stylePreset == uistyle::Preset::Dark;   // uPreset=2: Custom (Dark colours by default)
		bool installable = true;
		if (custom) {
			LoadColours(folder / L"CustomColours.ini");
			std::string json;
			const auto manifest = ReadText(source / L"MRCustom_P.json", json) ? custompack::ParseManifest(json) : custompack::Manifest{};
			const auto decision = custompack::Decide(manifest, a_gameBuild, darkstyle::Get());
			installable = decision.install;
			switch (decision.verdict) {
			case custompack::Verdict::ColoursChanged:
				logger::warn("Menus Revamped: Custom pack colours changed since the pack was built ({}): the last pack stays installed; rebuild the pack to bake them",
					Join(decision.changed));
				break;
			case custompack::Verdict::WrongBuild:
				logger::warn("Menus Revamped: Custom pack was built for game {}, this is game {}: not installed; rebuild the pack",
					manifest.gameBuild, a_gameBuild);
				break;
			case custompack::Verdict::Unreadable:
				logger::warn("Menus Revamped: Custom pack manifest MRCustom_P.json is missing or unreadable: not installed (its game build cannot be checked)");
				break;
			default: break;
			}
		}
		const auto outcome = darkpackswitch::SwitchCustom(source, mods, custom, installable);
		g_active = darkpackswitch::Active(outcome);
		const bool problem = outcome == darkpackswitch::Outcome::Incomplete || outcome == darkpackswitch::Outcome::Failed;
		if (problem)
			logger::warn("Menus Revamped: Custom UI pack {}", Describe(outcome));
		else
			logger::info("Menus Revamped: Custom UI pack {}", Describe(outcome));
	}

	bool Active() { return g_active; }
}
