// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DarkPack.h"
#include "core/Config.h"
#include "core/rules/CustomColours.h"
#include "core/rules/CustomPackManifest.h"
#include "core/rules/DarkPackSwitch.h"
#include "core/rules/LayoutPack.h"
#include <fstream>
#include <iterator>

namespace darkpack
{
	namespace
	{
		bool g_active = false;
		bool g_layoutBaked = false;
		bool g_bakedLaurelTint = false;

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
		g_bakedLaurelTint = false;
		if (custom) {
			LoadColours(folder / L"CustomColours.ini");
			std::string json;
			const auto manifest = ReadText(source / L"MRCustom_P.json", json) ? custompack::ParseManifest(json) : custompack::Manifest{};
			const auto decision = custompack::Decide(manifest, a_gameBuild, darkstyle::Get());
			installable = decision.install;
			g_bakedLaurelTint = installable && custompack::HasBakedLaurelTint(manifest);
			if (installable && !g_bakedLaurelTint)
				logger::info("Menus Revamped: Custom pack uses the runtime Inventory laurel tint; rebuild to bake it");
			if (installable && !custompack::HasBakedScrollbar(manifest))
				logger::info("Menus Revamped: Custom pack has no baked scrollbar colours (the stock scrollbar shows); rebuild the pack");
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
		// Step B (decision 46): MRCustom_P carries the sizes too, so exactly one of the two packs goes into ~mods
		std::string customJson, layoutJson;
		const bool customSizes = ReadText(source / L"MRCustom_P.json", customJson) && layoutpack::HasSizes(customJson);
		const bool layoutAllowed = ReadText(source / L"MRLayout_P.json", layoutJson) && layoutpack::LayoutManifestOk(layoutJson, a_gameBuild);
		const bool layoutOn = mrconfig::Get().layoutPack;
		const auto outcomes = layoutpack::Switch(source, mods, custom, installable, layoutOn, layoutAllowed);
		g_active = darkpackswitch::Active(outcomes.custom);
		g_layoutBaked = (g_active && customSizes) || darkpackswitch::Active(outcomes.layout);
		const bool problem = outcomes.custom == darkpackswitch::Outcome::Incomplete || outcomes.custom == darkpackswitch::Outcome::Failed;
		if (problem)
			logger::warn("Menus Revamped: Custom UI pack {}", Describe(outcomes.custom));
		else
			logger::info("Menus Revamped: Custom UI pack {}", Describe(outcomes.custom));
		if (g_active && layoutOn && !customSizes)
			logger::info("Menus Revamped: Custom pack has no baked sizes (built before Step B or with bLayoutPack=0); rebuild it to bake them");
		if (outcomes.layout == darkpackswitch::Outcome::Failed)
			logger::warn("Menus Revamped: sizes pack MRLayout_P could not be copied into ~mods; nothing of it is left there");
		else if (darkpackswitch::Active(outcomes.layout))
			logger::info("Menus Revamped: sizes pack MRLayout_P {}", Describe(outcomes.layout));
		else if (layoutOn && !g_active && !layoutAllowed)
			logger::info("Menus Revamped: sizes pack MRLayout_P not installed (not built for game {} yet: run the Pack Builder)", a_gameBuild);
		else
			logger::info("Menus Revamped: sizes pack MRLayout_P not in ~mods ({})", g_active ? "the Custom pack carries the sizes" :
				layoutOn ? "not built" : "[Layout] bLayoutPack=0");
		logger::info("Menus Revamped: sizes {}", g_layoutBaked ? "baked in the installed pack" : "not baked (the game's own layout)");
	}

	bool Active() { return g_active; }
	bool LayoutBaked() { return g_layoutBaked; }
	bool BakedLaurelTint() { return g_active && g_bakedLaurelTint; }
}
