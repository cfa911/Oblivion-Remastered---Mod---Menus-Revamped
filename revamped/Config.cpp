#include "Config.h"

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
		g_values.returnToListAfterFilter =
			GetPrivateProfileIntW(L"Controller", L"bReturnToListAfterFilter", 1, ini.c_str()) != 0;
	}

	const Values& Get() { return g_values; }
}
