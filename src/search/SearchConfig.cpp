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
			v.inventory = ReadInt(ini, L"Search", L"bInventory", v.inventory ? 1 : 0, 0, 1) != 0;
			v.magic = ReadInt(ini, L"Search", L"bMagic", v.magic ? 1 : 0, 0, 1) != 0;
			v.quests = ReadInt(ini, L"Search", L"bQuests", v.quests ? 1 : 0, 0, 1) != 0;
			v.container = ReadInt(ini, L"Search", L"bContainer", v.container ? 1 : 0, 0, 1) != 0;
			v.ime = ReadInt(ini, L"Search", L"bIme", v.ime ? 1 : 0, 0, 1) != 0;
			v.ignoreAccents = ReadInt(ini, L"Search", L"bIgnoreAccents", v.ignoreAccents ? 1 : 0, 0, 1) != 0;
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
				v.magicBar = place(L"Magic", { v.barX + 3, v.barY + v.barYMagic, v.barW - 3, v.barH }, false);   // 3 off the left edge
				v.yoursBar = place(L"ContainerYours", v.yoursBar, true);
				v.theirsBar = place(L"ContainerTheirs", v.theirsBar, true);
				v.takeBar = place(L"ContainerTake", v.takeBar, true);
				v.questBar = place(L"Quest", v.magicBar, false);   // the Magic menu's place (as it is set above) unless [Quest] says otherwise
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
