#pragma once

#include "ItemTags.h"

// Pure popup transitions. Navigating parents/back never commits a new filter;
// only a leaf sets category/choice. Flat applied IDs work with the existing masks/cache.
namespace pickernav
{
	using itemtags::Tab;
	struct Result
	{
		Tab view;
		unsigned category, choice, cursor = 0;
		bool close = false;
	};
	inline Result Choose(Tab root, Tab view, unsigned category, unsigned choice, unsigned command)
	{
		Result out{ view, category, choice };
		if (command >= itemtags::Info(view).count) return out;
		if (root == Tab::AllItems && view == Tab::AllItems && command > 0) {
			out.view = static_cast<Tab>(command - 1);
			return out;
		}
		if (view == Tab::Potions && command == 1) {
			out.view = Tab::PotionEffects;
			return out;
		}
		if (view == Tab::PotionEffects) {
			out.category = root == Tab::AllItems ? 3u : 0u;
			out.choice = itemtags::PotionSelection(command);
		} else if (root == Tab::AllItems) {
			out.category = view == Tab::AllItems ? 0u : static_cast<unsigned>(view) + 1;
			out.choice = command;
		} else out.choice = command;
		out.close = true;
		return out;
	}
	inline Result Back(Tab root, Tab view, unsigned category, unsigned choice)
	{
		Result out{ view, category, choice };
		if (view == Tab::PotionEffects) {
			out.view = Tab::Potions;
			out.cursor = 1;
		} else if (root == Tab::AllItems && view != root) {
			out.view = root;
			out.cursor = static_cast<unsigned>(view) + 1;
		} else out.close = true;
		return out;
	}
	constexpr unsigned Highlight(Tab root, Tab view, unsigned category, unsigned choice)
	{
		if (root == Tab::AllItems) {
			if (view == root) return category;
			const unsigned viewCategory = view == Tab::PotionEffects ? 3u : static_cast<unsigned>(view) + 1;
			if (category != viewCategory) return 99;
		}
		if (view == Tab::Potions) return choice >= itemtags::Index(itemtags::Alchemy::Health) ? 1u : choice;
		if (view == Tab::PotionEffects) {
			if (choice == itemtags::Index(itemtags::Alchemy::Potions)) return 0;
			return choice >= itemtags::Index(itemtags::Alchemy::Health) &&
				choice <= itemtags::Index(itemtags::Alchemy::Poison) ? choice - 3 : 99;
		}
		return choice;
	}
}
