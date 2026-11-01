#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "engine/TextUtil.h"

namespace maprules
{
	struct Location
	{
		std::string id, key, address, name;
		double x = 0, y = 0;
		int type = 0;
		bool discovered = false;
		bool passesNativeFilter = true;
	};
	inline bool Eligible(const Location& l) { return l.discovered && l.passesNativeFilter && l.type >= 1 && l.type <= 12 && !l.name.empty() && !l.id.empty(); }
	inline std::vector<Location> Filter(const std::vector<Location>& locations, const std::string& query, bool accents)
	{
		std::vector<Location> out;
		if (query.empty()) return out;
		for (const auto& l : locations) if (Eligible(l) && textutil::Contains(l.name, query, accents)) out.push_back(l);
		std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
			const auto an = textutil::Lower(a.name), bn = textutil::Lower(b.name);
			return an != bn ? an < bn : a.id < b.id;
		});
		return out;
	}
	inline const Location* Resolve(const std::vector<Location>& locations, const std::string& id)
	{
		for (const auto& l : locations) if (l.id == id && Eligible(l)) return &l;
		return nullptr;
	}
}
