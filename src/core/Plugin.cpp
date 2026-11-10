// Menus Revamped: search, category pickers and controller keyboard.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Runtime.h"
#include "core/Config.h"
#include "core/DarkPack.h"
#include "search/Search.h"

namespace
{
	// TEMP-DIAG (pak mount timing, 2026-11-10): is a pak already held open by the engine at this point? An exclusive open fails
	// with a sharing violation once the engine has mounted it; a successful open is closed at once (nothing is changed).
	// Decides whether the plugin can switch a Dark texture pack in or out at startup from the INI. Record the result in the KB.
	void PakMountProbe(const char* when)
	{
		const auto content = mrconfig::PluginFolder().parent_path().parent_path().parent_path().parent_path() / L"Content" / L"Paks";
		// .ucas (container data, kept open once mounted); the .utoc is read into memory and closed (16:04 log: 'not open' at PostLoad)
		for (const auto& file : { content / L"OblivionRemastered-Windows.ucas", content / L"global.ucas", content / L"~mods" / L"MRSkinHighlight_P.ucas" }) {
			const HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			const DWORD error = handle == INVALID_HANDLE_VALUE ? GetLastError() : 0;
			if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
			logger::warn("TEMP-DIAG MR-PAK-PROBE {}: {} {}", when, file.filename().string(),
				error == 0 ? "not open yet (not mounted)" : error == ERROR_SHARING_VIOLATION ? "held open (mounted)" : "error " + std::to_string(error));
		}
	}

	void OnMessage(OBSE::MessagingInterface::Message* message)
	{
		if (message && message->type == OBSE::MessagingInterface::kDataLoaded) PakMountProbe("DataLoaded");
		if (message && message->type == OBSE::MessagingInterface::kPostLoad) {
			PakMountProbe("PostLoad");
			search::SetGameThread(GetCurrentThreadId());
			input::Install();
		}
	}
}

extern "C"
{
	__declspec(dllexport) bool MenusRevamped_IsTyping() { return search::Typing(); }
	__declspec(dllexport) bool MenusRevamped_CursorOverBar() { return search::CursorOverBar(); }
	__declspec(dllexport) bool MenusRevamped_IsControllerKeyboardOpen() { return core::ControllerKeyboardOpen(); }
}

OBSE_PLUGIN_LOAD(const OBSE::LoadInterface* obse)
{
	OBSE::Init(obse, { .logRotate = 3 });
	mrconfig::Load();
	const auto level = static_cast<spdlog::level::level_enum>(std::clamp(mrconfig::Get().search.logLevel, 0, 4));
	logger::set_level(level, level);
	PakMountProbe("plugin load");
	darkpack::Apply();   // before the engine mounts ~mods (MR-PAK-PROBE)
	logger::info("MR-MAP-SEARCH-20261101: world-map results and native marker focus");
	logger::warn("Menus Revamped MR-SMOOTH-20261027-R3: latest-side quiescent hover replay; integrated hover throttle and object guard; standalone SmoothMenus must be disabled");
	logger::warn("Menus Revamped MR-INTEGRATION-20261026: unified search/pickers/controller keyboard; one list owner; no display-copy/input-freeze experiment");
	if (auto* messages = OBSE::GetMessagingInterface()) return messages->RegisterListener(&OnMessage);
	return false;
}
