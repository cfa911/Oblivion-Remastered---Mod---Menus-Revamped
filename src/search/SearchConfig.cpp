#include "search/SearchConfig.h"
#include "core/Config.h"

namespace searchcfg
{
	using mrconfig::ReadDouble;
	using mrconfig::ReadInt;

	Values ReadIni(const std::filesystem::path& ini)
	{
		Values     v;
		if (std::filesystem::exists(ini)) {
			v.logLevel = ReadInt(ini, L"Log", L"uLogLevel", v.logLevel, 0, 4);
			v.alchemy = ReadInt(ini, L"Search", L"bAlchemy", v.alchemy ? 1 : 0, 0, 1) != 0;
			v.worldMap = ReadInt(ini, L"Search", L"bWorldMap", v.worldMap ? 1 : 0, 0, 1) != 0;
			v.inventory = ReadInt(ini, L"Search", L"bInventory", v.inventory ? 1 : 0, 0, 1) != 0;
			v.magic = ReadInt(ini, L"Search", L"bMagic", v.magic ? 1 : 0, 0, 1) != 0;
			v.quests = ReadInt(ini, L"Search", L"bQuests", v.quests ? 1 : 0, 0, 1) != 0;
			v.container = ReadInt(ini, L"Search", L"bContainer", v.container ? 1 : 0, 0, 1) != 0;
			v.ime = ReadInt(ini, L"Search", L"bIme", v.ime ? 1 : 0, 0, 1) != 0;
			v.ignoreAccents = ReadInt(ini, L"Search", L"bIgnoreAccents", v.ignoreAccents ? 1 : 0, 0, 1) != 0;
			v.clearTextOnGameplay = ReadInt(ini, L"Search", L"bClearTextOnGameplay", v.clearTextOnGameplay ? 1 : 0, 0, 1) != 0;
			v.hotkey = ReadInt(ini, L"Search", L"uHotkey", v.hotkey, 0, 0xFE);
			v.hotkeyCtrl = ReadInt(ini, L"Search", L"bHotkeyCtrl", v.hotkeyCtrl ? 1 : 0, 0, 1) != 0;
			v.barFontSize = ReadInt(ini, L"Bar", L"uFontSize", v.barFontSize, 8, 60);
			v.barX = ReadDouble(ini, L"Bar", L"uX", v.barX, 0, 3000);
			v.barY = ReadDouble(ini, L"Bar", L"uY", v.barY, 0, 3000);
			v.barYInventory = ReadDouble(ini, L"Bar", L"uYInventory", v.barYInventory, -200, 200);
			v.barYMagic = ReadDouble(ini, L"Bar", L"uYMagic", v.barYMagic, -200, 200);
			v.barW = ReadDouble(ini, L"Bar", L"uWidth", v.barW, 50, 3000);
			v.barH = ReadDouble(ini, L"Bar", L"uHeight", v.barH, 10, 400);
			{   // each bar has its own place; the [Bar] keys (what 1.0.0 had) are the defaults of the Inventory and the Magic menu
				const auto place = [&](const wchar_t* a_section, searchcfg::Values::Place a_default, bool a_offset) {
					searchcfg::Values::Place p = a_default;
					const double lo = a_offset ? -1500 : 0, hi = a_offset ? 1500 : 3000;
					p.x = ReadDouble(ini, a_section, a_offset ? L"uXOffset" : L"uX", p.x, lo, hi);
					p.y = ReadDouble(ini, a_section, a_offset ? L"uYOffset" : L"uY", p.y, lo, hi);
					p.w = ReadDouble(ini, a_section, L"uWidth", p.w, a_offset ? 0 : 50, 3000);
					p.h = ReadDouble(ini, a_section, L"uHeight", p.h, 10, 400);
					return p;
				};
				v.inventoryBar = place(L"Inventory", { v.barX + 2, v.barY + v.barYInventory, v.barW - 2, v.barH }, false);   // 2 off the left edge
				// Magic revamp: the bar sits in the title row, right of the page title (probe 2026-11-09: title row 248.2-304.4,
				// list right edge 904.3), like the Inventory bar.
				v.magicBar = place(L"Magic", { 440, 258, 460, 36 }, false);
				// Step C.1: the side bars sit in the title row like the Inventory bar and take its size. The old keys (uXOffset/uYOffset/
				// uWidth/uHeight: a place above the header, shipped as -48/-90/825/37) are not read: they moved the bar out of the row.
				const auto rowOffset = [&](const wchar_t* a_section) {
					searchcfg::Values::Place p{};
					p.x = ReadDouble(ini, a_section, L"uTitleRowXOffset", 0, -1500, 1500);
					p.y = ReadDouble(ini, a_section, L"uTitleRowYOffset", 0, -1500, 1500);
					return p;
				};
				v.yoursBar = rowOffset(L"ContainerYours");
				v.theirsBar = rowOffset(L"ContainerTheirs");
				v.mapBar = place(L"Map", v.mapBar, false);
				v.takeBar = rowOffset(L"ContainerTake");
				// The quests tab keeps the Magic bar's earlier place (above the tabs) unless [Quest] says otherwise.
				v.questBar = place(L"Quest", { v.barX + 3, v.barY + v.barYMagic, v.barW - 3, v.barH }, false);
			}
			v.ownFilter = ReadInt(ini, L"Filter", L"bEnabled", v.ownFilter ? 1 : 0, 0, 1) != 0;
			logger::info("config: {} read", ini.string());
		} else {
			logger::warn("config: {} not found - compiled defaults in use", ini.string());
		}

		logger::info("config: search in the Inventory {}, in the Magic menu {},in the container / barter menu {}, in the quests tab {}, filter {}", v.inventory ? "on" : "off", v.magic ? "on" : "off", v.container ? "on" : "off", v.quests ? "on" : "off", v.ownFilter ? "on" : "off");
		return v;
	}


	const Values& Get() { return mrconfig::Get().search; }
}
