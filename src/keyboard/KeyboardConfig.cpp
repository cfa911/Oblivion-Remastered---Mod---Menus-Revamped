#include "keyboard/KeyboardConfig.h"
#include "core/Config.h"

namespace kbcfg
{
	namespace
	{




		int ReadInt(const std::filesystem::path& a_ini, const wchar_t* a_section, const wchar_t* a_key, int a_default, int a_min, int a_max)
		{
			const int v = static_cast<int>(GetPrivateProfileIntW(a_section, a_key, a_default, a_ini.c_str()));
			return std::clamp(v, a_min, a_max);
		}
	}

	Values ReadIni(const std::filesystem::path& ini)
	{
		Values     v;
		if (std::filesystem::exists(ini)) {
			v.enabled = ReadInt(ini, L"SearchKeyboard", L"bEnabled", v.enabled ? 1 : 0, 0, 1) != 0;
			v.button = ReadInt(ini, L"SearchKeyboard", L"uButton", v.button, 0, 0xFFFF);
			v.buttonLeft = ReadInt(ini, L"SearchKeyboard", L"uButtonLeft", v.buttonLeft, 0, 0xFFFF);
			v.holdMs = ReadInt(ini, L"SearchKeyboard", L"uHoldMs", v.holdMs, 150, 2000);
			v.leftPercent = ReadInt(ini, L"SearchKeyboard", L"uLeftPercent", v.leftPercent, 0, 90);
			v.topPercent = ReadInt(ini, L"SearchKeyboard", L"uTopPercent", v.topPercent, 0, 90);
			v.rightPercent = ReadInt(ini, L"SearchKeyboard", L"uRightPercent", v.rightPercent, v.leftPercent + 10, 100);
			v.bottomPercent = ReadInt(ini, L"SearchKeyboard", L"uBottomPercent", v.bottomPercent, v.topPercent + 10, 100);
			v.textPercent = ReadInt(ini, L"SearchKeyboard", L"uTextPercent", v.textPercent, 30, 300);
			logger::info("config: {} read", ini.string());
		} else {
			logger::warn("config: {} not found - compiled defaults in use", ini.string());
		}

		logger::info("config: keyboard {} (hold 0x{:04X} for {} ms, area {}%,{}% to {}%,{}%, text {}%)", v.enabled ? "on" : "off", v.button, v.holdMs,
			v.leftPercent, v.topPercent, v.rightPercent, v.bottomPercent, v.textPercent);
		return v;
	}


	std::filesystem::path PluginFolder() { return mrconfig::PluginFolder(); }


	const Values& Get() { return mrconfig::Get().keyboard; }
}
