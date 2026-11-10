// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DarkPack.h"
#include "core/Config.h"
#include "core/rules/DarkPackSwitch.h"

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
			case darkpackswitch::Outcome::Removed: return "removed from ~mods (preset is not Dark)";
			case darkpackswitch::Outcome::Absent: return "not installed (preset is not Dark)";
			case darkpackswitch::Outcome::Incomplete: return "missing a file in Plugins/MenusRevamped/DarkPack: not installed";
			default: return "could not be copied or removed (file in use or no permission): not installed";
			}
		}
	}

	void Apply()
	{
		const auto plugins = mrconfig::PluginFolder();   // ...\Binaries\Win64\OBSE\Plugins
		const auto source = plugins / L"MenusRevamped" / L"DarkPack";
		const auto mods = plugins.parent_path().parent_path().parent_path().parent_path() / L"Content" / L"Paks" / L"~mods";
		const bool dark = mrconfig::Get().stylePreset == uistyle::Preset::Dark;
		const auto outcome = darkpackswitch::Switch(source, mods, dark);
		g_active = darkpackswitch::Active(outcome);
		const bool problem = outcome == darkpackswitch::Outcome::Incomplete || outcome == darkpackswitch::Outcome::Failed;
		if (problem)
			logger::warn("Menus Revamped: Dark UI pack {}", Describe(outcome));
		else
			logger::info("Menus Revamped: Dark UI pack {}", Describe(outcome));
	}

	bool Active() { return g_active; }
}
