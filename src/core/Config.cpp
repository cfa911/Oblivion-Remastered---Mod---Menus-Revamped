#include "core/Config.h"

namespace mrconfig
{
	namespace { Values g_values; }

	std::filesystem::path PluginFolder()
	{
		HMODULE module = nullptr;
		std::array<wchar_t, 32768> path{};
		if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&PluginFolder), &module) &&
			GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size())))
			return std::filesystem::path(path.data()).parent_path();
		return {};
	}

	void Load()
	{
		const auto ini = PluginFolder() / L"MenusRevamped.ini";
		g_values.search = searchcfg::ReadIni(ini);
		g_values.keyboard = kbcfg::ReadIni(ini);
		g_values.smooth = smoothcfg::ReadIni(ini);
		g_values.returnToListAfterFilter =
			GetPrivateProfileIntW(L"Controller", L"bReturnToListAfterFilter", 1, ini.c_str()) != 0;
	}

	const Values& Get() { return g_values; }

	int ReadInt(const std::filesystem::path& a_ini, const wchar_t* a_section, const wchar_t* a_key, int a_default, int a_min, int a_max)
	{
		const int v = static_cast<int>(GetPrivateProfileIntW(a_section, a_key, a_default, a_ini.c_str()));
		return std::clamp(v, a_min, a_max);
	}

	double ReadDouble(const std::filesystem::path& a_ini, const wchar_t* a_section, const wchar_t* a_key, double a_default, double a_min, double a_max)
	{
		wchar_t buf[64]{};
		GetPrivateProfileStringW(a_section, a_key, L"", buf, 64, a_ini.c_str());
		if (!buf[0]) return a_default;
		wchar_t*     end = nullptr;
		const double v = std::wcstod(buf, &end);
		return end == buf ? a_default : std::clamp(v, a_min, a_max);
	}
}


namespace smoothcfg
{
	Values ReadIni(const std::filesystem::path& ini)
	{
		Values v;
		const auto read = [&](const wchar_t* section, const wchar_t* key, int fallback, int low, int high) {
			return mrconfig::ReadInt(ini, section, key, fallback, low, high);
		};
		v.inventoryHover = read(L"Hover", L"bInventory", 1, 0, 1) != 0;
		v.magicHover = read(L"Hover", L"bMagic", 1, 0, 1) != 0;
		v.containerHover = read(L"Hover", L"bContainer", 1, 0, 1) != 0;
		v.hoverIntervalMs = read(L"Hover", L"uIntervalMs", 200, 0, 1000);
		v.objectGuard = read(L"Guard", L"bEnabled", 1, 0, 1) != 0;
		v.objectGuardLimit = read(L"Guard", L"uLimit", 1500000, 200000, 2100000);
		v.objectGuardCooldown = read(L"Guard", L"uCooldown", 10, 1, 600);
		v.metrics = read(L"Metrics", L"bEnabled", 0, 0, 1) != 0;
		return v;
	}
}
