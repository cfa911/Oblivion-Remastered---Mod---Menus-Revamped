#pragma once

#include "sorting/rules/ItemTags.h"

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
		if (view == Tab::Potions && (command == 1 || command == 2)) {
			out.view = command == 1 ? Tab::PotionEffects : Tab::PoisonEffects;
			return out;
		}
		if (view == Tab::PotionEffects || view == Tab::PoisonEffects) {
			out.category = root == Tab::AllItems ? 3u : 0u;
			out.choice = view == Tab::PotionEffects ? itemtags::PotionSelection(command) : itemtags::PoisonSelection(command);
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
		if (view == Tab::PotionEffects || view == Tab::PoisonEffects) {
			out.view = Tab::Potions;
			out.cursor = view == Tab::PotionEffects ? 1u : 2u;
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
			const unsigned viewCategory = view == Tab::PotionEffects || view == Tab::PoisonEffects ? 3u : static_cast<unsigned>(view) + 1;
			if (category != viewCategory) return 99;
		}
		using itemtags::Alchemy;
		using itemtags::Index;
		if (view == Tab::Potions) {
			if (itemtags::IsPotionGroup(choice)) return Index(Alchemy::Potions);
			if (itemtags::IsPoisonGroup(choice)) return Index(Alchemy::Poisons);
			return choice;
		}
		if (view == Tab::PotionEffects) {
			if (choice == Index(Alchemy::Potions)) return 0;
			return itemtags::IsPotionGroup(choice) ? choice - Index(Alchemy::Restore) + 1 : 99;
		}
		if (view == Tab::PoisonEffects) {
			if (choice == Index(Alchemy::Poisons)) return 0;
			return itemtags::IsPoisonGroup(choice) ? choice - Index(Alchemy::Damage) + 1 : 99;
		}
		return choice;
	}
}
