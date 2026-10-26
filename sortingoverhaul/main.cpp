// Sorting Overhaul - independent OBSE64 plugin. SPDX-License-Identifier: GPL-3.0-or-later
#include "Runtime.h"
namespace {
	void OnMessage(OBSE::MessagingInterface::Message* message)
	{
		if (message && message->type == OBSE::MessagingInterface::kPostLoad) sortingdriver::Install();
	}
}
extern "C" __declspec(dllexport) bool SortingOverhaul_FilterList(int menu, UE::UObject* list, const char* query, bool (*matches)(UE::UObject*, const char*))
{
	return sorting::FilterList(menu, list, query, matches);
}
OBSE_PLUGIN_LOAD(const OBSE::LoadInterface* obse)
{
	OBSE::Init(obse);
	logger::set_level(spdlog::level::warn, spdlog::level::warn);
	logger::info("Sorting Overhaul: SO-POTION-MAGICKA-20261025 uses verified RESP/FOSP potion effects");
	logger::info("Sorting Overhaul: SO-WEAPON-KINDS-20261025 uses bounded model/icon hints with Other fallback");
	logger::info("Sorting Overhaul: SO-ALCHEMY-TREE-20261025 nests potion families; Ingredients/Tools and Misc hammer/torch tools");

	if (auto* messages = OBSE::GetMessagingInterface()) return messages->RegisterListener(&OnMessage);
	return false;
}
