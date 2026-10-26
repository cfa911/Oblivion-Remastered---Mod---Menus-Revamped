#pragma once
namespace sorting {
	void Tick();
	void LoadSettings();
	bool ReturnToListAfterFilter();
	bool FilterList(int, UE::UObject*, const char*, bool (*)(UE::UObject*, const char*));
}
namespace sortingdriver { bool Install(); }
