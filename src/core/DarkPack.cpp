// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DarkPack.h"
#include "core/Config.h"
#include "core/rules/CustomColours.h"
#include "core/rules/CustomPackManifest.h"
#include "core/rules/DarkPackSwitch.h"
#include "core/rules/LayoutPack.h"
#include "core/rules/PresetPacks.h"
#include <fstream>
#include <iterator>

namespace darkpack
{
	namespace
	{
		bool g_active = false;
		bool g_layoutBaked = false;
		bool g_headerLayoutBaked = false;
		bool g_headerOverflowBaked = false;
		bool g_bakedLaurelTint = false;

		const char* Describe(darkpackswitch::Outcome outcome)
		{
			switch (outcome) {
			case darkpackswitch::Outcome::Installed: return "copied into ~mods";
			case darkpackswitch::Outcome::UpToDate: return "already in ~mods";
			case darkpackswitch::Outcome::Removed: return "removed from ~mods";
			case darkpackswitch::Outcome::Absent: return "not in ~mods";
			case darkpackswitch::Outcome::Incomplete: return "missing a file in its Plugins/MenusRevamped folder: not installed";
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
		const auto darkSource = folder / L"DarkPack";       // shipped in the zip: the default colours (Dark; Custom until built)
		const auto customSource = folder / L"CustomPack";   // the Pack Builder's output, never shipped (MRCustom_P, MRLayout_P)
		const auto mods = plugins.parent_path().parent_path().parent_path().parent_path() / L"Content" / L"Paks" / L"~mods";
		const auto preset = mrconfig::Get().stylePreset;
		g_bakedLaurelTint = false;

		// Custom: the player's colour file and pack
		std::string customJson;
		custompack::Manifest customManifest{};
		bool customAllowed = false;
		if (preset == uistyle::Preset::Custom) {
			LoadColours(folder / L"CustomColours.ini");
			const bool present = presetpacks::Present(customSource, presetpacks::kCustomPack);
			const bool readable = ReadText(customSource / L"MRCustom_P.json", customJson);
			if (!present && !readable) {
				logger::info("Menus Revamped: Custom preset: no pack built yet (run the Pack Builder): the Dark look is shown");
			} else {
				customManifest = readable ? custompack::ParseManifest(customJson) : custompack::Manifest{};
				const auto decision = custompack::Decide(customManifest, a_gameBuild, darkstyle::Get());
				customAllowed = present && decision.install;
				switch (decision.verdict) {
				case custompack::Verdict::ColoursChanged:
					logger::warn("Menus Revamped: Custom pack colours changed since the pack was built ({}): the last pack stays installed; rebuild the pack to bake them",
						Join(decision.changed));
					break;
				case custompack::Verdict::WrongBuild:
					logger::warn("Menus Revamped: Custom pack was built for game {}, this is game {}: the Dark look is shown; rebuild the pack",
						customManifest.gameBuild, a_gameBuild);
					break;
				case custompack::Verdict::Unreadable:
					logger::warn("Menus Revamped: Custom pack manifest MRCustom_P.json is missing or unreadable: the Dark look is shown (its game build cannot be checked)");
					break;
				default: break;
				}
				if (!present && decision.install)
					logger::warn("Menus Revamped: Custom pack is missing a file in Plugins/MenusRevamped/CustomPack: the Dark look is shown; rebuild the pack");
			}
		}

		// Dark, and Custom until the player built a pack: the shipped pack, built from the default colours
		std::string darkJson;
		custompack::Manifest darkManifest{};
		bool darkAllowed = false;
		if (preset != uistyle::Preset::Classic && !customAllowed) {
			const bool present = presetpacks::Present(darkSource, presetpacks::kDarkPack);
			darkManifest = ReadText(darkSource / L"MRDark_P.json", darkJson) ? custompack::ParseManifest(darkJson) : custompack::Manifest{};
			const auto decision = custompack::Decide(darkManifest, a_gameBuild, darkstyle::kDefaults);
			darkAllowed = present && decision.install;
			if (!present)
				logger::warn("Menus Revamped: Dark pack is missing a file in Plugins/MenusRevamped/DarkPack: not installed (reinstall the mod)");
			else if (decision.verdict == custompack::Verdict::WrongBuild)
				logger::warn("Menus Revamped: Dark pack was built for game {}, this is game {}: not installed (a mod update is needed)",
					darkManifest.gameBuild, a_gameBuild);
			else if (decision.verdict == custompack::Verdict::Unreadable)
				logger::warn("Menus Revamped: Dark pack manifest MRDark_P.json is missing or unreadable: not installed (reinstall the mod)");
		}

		const auto colours = presetpacks::Choose(static_cast<int>(preset), customAllowed, darkAllowed);
		const bool usesCustom = colours == presetpacks::Colours::Custom;
		if (!usesCustom) darkstyle::Set(darkstyle::kDefaults);   // the run-time colours match the pack: the defaults unless the player's
		const auto& manifest = usesCustom ? customManifest : darkManifest;
		const auto& colourJson = usesCustom ? customJson : darkJson;
		const bool installable = colours != presetpacks::Colours::None;
		g_bakedLaurelTint = installable && custompack::HasBakedLaurelTint(manifest);
		if (installable && !g_bakedLaurelTint)
			logger::info("Menus Revamped: the colour pack uses the runtime Inventory laurel tint; rebuild to bake it");
		if (installable && !custompack::HasBakedScrollbar(manifest))
			logger::info("Menus Revamped: the colour pack has no baked scrollbar colours (the stock scrollbar shows); rebuild the pack");

		// Step B (decision 46): a colour pack may carry the sizes too, so exactly one pack goes into ~mods
		std::string layoutJson;
		const bool colourSizes = installable && layoutpack::HasSizes(colourJson);
		const bool layoutAllowed = ReadText(customSource / L"MRLayout_P.json", layoutJson) && layoutpack::LayoutManifestOk(layoutJson, a_gameBuild);
		const bool layoutOn = mrconfig::Get().layoutPack;
		const auto outcomes = presetpacks::Switch(darkSource, customSource, mods, colours, layoutOn, layoutAllowed);
		const auto colourOutcome = usesCustom ? outcomes.custom : outcomes.dark;
		g_active = installable && darkpackswitch::Active(colourOutcome);
		g_layoutBaked = (g_active && colourSizes) || darkpackswitch::Active(outcomes.layout);
		g_headerLayoutBaked = g_layoutBaked && layoutpack::HasHeaderLayout(g_active ? colourJson : layoutJson);
		g_headerOverflowBaked = g_headerLayoutBaked && layoutpack::HasHeaderOverflow(g_active ? colourJson : layoutJson);
		const char* name = usesCustom ? "Custom pack MRCustom_P" : "Dark pack MRDark_P";
		if (!installable)
			logger::info("Menus Revamped: no colour pack in ~mods ({})", preset == uistyle::Preset::Classic ? "Classic preset" : "the Dark pack is not usable, see above");
		else if (colourOutcome == darkpackswitch::Outcome::Incomplete || colourOutcome == darkpackswitch::Outcome::Failed)
			logger::warn("Menus Revamped: {} {}", name, Describe(colourOutcome));
		else
			logger::info("Menus Revamped: {} {}", name, Describe(colourOutcome));
		if (outcomes.dark == darkpackswitch::Outcome::Failed || outcomes.custom == darkpackswitch::Outcome::Failed)
			logger::warn("Menus Revamped: a colour pack could not be copied or removed in ~mods (file in use or no permission)");
		if (g_active && layoutOn && !colourSizes)
			logger::info("Menus Revamped: the colour pack has no baked sizes{}", usesCustom ? " (built before Step B or with bLayoutPack=0); rebuild it to bake them" : "");
		if (outcomes.layout == darkpackswitch::Outcome::Failed)
			logger::warn("Menus Revamped: sizes pack MRLayout_P could not be copied into ~mods; nothing of it is left there");
		else if (darkpackswitch::Active(outcomes.layout))
			logger::info("Menus Revamped: sizes pack MRLayout_P {}", Describe(outcomes.layout));
		else if (layoutOn && !g_active && !layoutAllowed)
			logger::info("Menus Revamped: sizes pack MRLayout_P not installed (not built for game {} yet: run the Pack Builder)", a_gameBuild);
		else
			logger::info("Menus Revamped: sizes pack MRLayout_P not in ~mods ({})", g_active ? "a colour pack is in ~mods" :
				layoutOn ? "not built" : "[Layout] bLayoutPack=0");
		logger::info("Menus Revamped: sizes {}", g_layoutBaked ? "baked in the installed pack" : "not baked (the game's own layout)");
	}

	bool Active() { return g_active; }
	bool LayoutBaked() { return g_layoutBaked; }
	bool HeaderLayoutBaked() { return g_headerLayoutBaked; }
	bool HeaderOverflowBaked() { return g_headerOverflowBaked; }
	bool BakedLaurelTint() { return g_active && g_bakedLaurelTint; }
}
