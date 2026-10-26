// Menus Revamped: search, category pickers and controller keyboard.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Runtime.h"
#include "../revamped/Config.h"
#include "../menusearch/MenuSearch.h"

namespace
{
	void OnMessage(OBSE::MessagingInterface::Message* message)
	{
		if (message && message->type == OBSE::MessagingInterface::kPostLoad) {
			msearch::SetGameThread(GetCurrentThreadId());
			sortingdriver::Install();
		}
	}
}

extern "C"
{
	__declspec(dllexport) bool MenusRevamped_IsTyping() { return msearch::Typing(); }
	__declspec(dllexport) bool MenusRevamped_CursorOverBar() { return msearch::CursorOverBar(); }
	__declspec(dllexport) bool MenusRevamped_IsControllerKeyboardOpen() { return sorting::ControllerKeyboardOpen(); }
}

OBSE_PLUGIN_LOAD(const OBSE::LoadInterface* obse)
{
	OBSE::Init(obse, { .logRotate = 3 });
	mrconfig::Load();
	const auto level = static_cast<spdlog::level::level_enum>(std::clamp(mrconfig::Get().search.logLevel, 0, 4));
	logger::set_level(level, level);
	logger::warn("Menus Revamped MR-INTEGRATION-20261026: unified search/pickers/controller keyboard; one list owner; no display-copy/input-freeze experiment");
	if (auto* messages = OBSE::GetMessagingInterface()) return messages->RegisterListener(&OnMessage);
	return false;
}
