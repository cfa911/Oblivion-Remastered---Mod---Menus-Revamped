#include "sorting/PickerInternal.h"

#include "core/Runtime.h"
#include "engine/ContainerMenu.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/TextUtil.h"
#include "engine/Ui.h"
#include "engine/UiText.h"
#include "sorting/GearData.h"
#include "sorting/Icons.h"

namespace sorting
{
	namespace detail
	{
		FilterState g_states[3];
		// Item tags depend only on the base item, so Inventory and both container sides share them across rebuilds.
		// Known rows stay filtered during a sort/transfer while the new TES request runs (RMI-005/RMI-015). Container rows have
		// null forms, so the key is the row's name, weight and type; a wrong match lasts only until the reply.
		std::unordered_map<std::uint64_t, std::uint64_t> g_rowTags;

		void ResetShared(unsigned slot)
		{
			geardata::Cancel(slot);
			g_states[slot] = {};
		}
	}

	using namespace detail;

	const std::vector<UE::UObject*>& Evaluate(int side,
		const std::vector<UE::UObject*>& source, std::uint64_t generation)
	{
		const unsigned slot = side >= 3 && side <= 4 ? static_cast<unsigned>(side - 2) : 0;
		auto& state = g_states[slot];
		auto& primary = g_pickers[slot ? slot + 1 : 0];
		auto& second = g_pickers[1];
		auto& type = g_pickers[4];
		const bool magic = side == 2;
		bool changed = false;
		if (state.generation != generation || state.menu != side) {
			geardata::Cancel(slot);
			state = {}; state.generation = generation; state.menu = side;
			std::vector<int> nativeIndices;
			std::vector<std::uint32_t> forms;
			for (auto* item : source) {
				state.indices.push_back(slot ? static_cast<int>(state.indices.size()) : Index(item));
				if (!magic) {
					std::uint64_t key = 0;
					const auto form = ContainerForm(item, &key);
					const auto index = state.indices.back();
					state.keys[index] = key;
					if (slot) { nativeIndices.push_back(Index(item)); forms.push_back(form); }
					if (const auto known = key ? g_rowTags.find(key) : g_rowTags.end(); known != g_rowTags.end())
						state.masks[index] = known->second;
				}
			}
			if (magic) { ComputeMagicMasks(source, state); state.dataReady = true; }
			else if (source.empty()) state.dataReady = true;
			else {
				state.ticket = geardata::Request(state.indices, std::move(forms), slot, std::move(nativeIndices));
				state.dataDeadline = GetTickCount64() + 2500;
			}
			changed = true;
		}
		std::vector<geardata::Entry> entries;
		if (!magic && state.ticket && geardata::Take(state.ticket, entries)) {
			state.dataReady = entries.size() == state.indices.size();
			if (state.dataReady) for (const auto& entry : entries) if (entry.valid) {
				state.masks[entry.index] = entry.tags;
				if (const auto key = state.keys.find(entry.index); key != state.keys.end() && key->second) {
					if (g_rowTags.size() > 16384) g_rowTags.clear();
					g_rowTags[key->second] = entry.tags;
				}
			}
			state.ticket = state.dataDeadline = 0;
			changed = true;
		}
		if (state.ticket && GetTickCount64() > state.dataDeadline) {
			geardata::Cancel(slot); state.ticket = state.dataDeadline = 0;
			changed = true;
			logger::warn("Menus Revamped: item classification timed out; unresolved categories remain visible (item-key cache RMI-015)");
		}
		const bool useSecond = second.kind == 2;
		const bool useType = magic && type.kind == 5;
		if (!changed && !state.dirty && primary.choice == primary.appliedChoice &&
			primary.category == primary.appliedCategory && primary.stolenOnly == primary.appliedStolen &&
			(!useSecond || second.choice == second.appliedChoice) &&
			(!useType || type.choice == type.appliedChoice))
			return state.filtered;
		const bool stolenOnly = primary.stolenOnly && stolenfilter::Supports(primary.kind) && (side == 1 || side == 3);
		if (stolenOnly && state.stolen.size() != source.size()) {
			state.stolen.clear();
			for (auto* item : source) state.stolen.push_back(StolenStatus(item));
		}
		state.filtered.clear();
		std::size_t position = 0;
		for (auto* item : source) {
			const auto at = position++;
			const auto mask = state.masks.find(slot ? static_cast<int>(at) : Index(item));
			if (!Active(side) || primary.kind < 0) { state.filtered.push_back(item); continue; }
			if (stolenOnly && !stolenfilter::Keeps(true, state.stolen[at])) continue;
			if (mask == state.masks.end() || (primary.Passes(mask->second) && (!useSecond || second.Passes(mask->second)) &&
				(!useType || type.Passes(mask->second))))
				state.filtered.push_back(item);
		}
		state.dirty = false;
		primary.appliedChoice = primary.choice; primary.appliedCategory = primary.category; primary.appliedStolen = primary.stolenOnly;
		second.appliedChoice = second.choice;
		if (useType) type.appliedChoice = type.choice;
		return state.filtered;
	}
}
