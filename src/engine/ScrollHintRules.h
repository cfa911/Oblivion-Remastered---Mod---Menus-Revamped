#pragma once
#include <string_view>
#define SCROLL_HINT_VISIBLE_WALK 1

namespace scrollhints::rules
{
	struct Box { double x, y, w, h; };
	inline bool Target(std::string_view name)
	{
		return name == "WBP_OriginalMenu_Inventory_C" || name == "WBP_ModernMenu_MagicMenu_C" ||
			name == "WBP_ModernMenu_StatsContent_C" || name == "WBP_Modern_QuestPage_C" || name == "WBP_ModernMenu_Help_C";
	}
	inline Box Below(Box list) { return { list.x + list.w - 38.0, list.y + list.h + 4.0, 36.0, 36.0 }; }
	inline bool Show(bool shown, bool gamepad, bool scrollable, bool glyph) { return shown && gamepad && scrollable && glyph; }
	// Prune before spending the budget; callers can stop at a menu scope or a native list instead of walking its contents.
	template<class Node, class Visible, class Children, class Visit>
	void WalkVisible(Node root, Visible visible, Children children, Visit visit, int& budget, int depth=0)
	{
		if (!root || depth>48 || budget<=0 || !visible(root)) return;
		--budget;
		if (!visit(root)) return;
		for (auto child:children(root)) WalkVisible(child,visible,children,visit,budget,depth+1);
	}
}
