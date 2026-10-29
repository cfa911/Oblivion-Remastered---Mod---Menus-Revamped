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
		// Container/barter tags depend only on the base item, so both sides share them across rebuilds: a transfer
		// rebuilds the list and known rows stay filtered while the new TES request runs (RMI-005). Rows there have
		// null forms, so the key is the row's name, weight and type; a wrong match lasts only until the reply.
		std::unordered_map<std::uint64_t, std::uint64_t> g_rowTags;

		void ResetShared(unsigned slot)
		{
			geardata::Cancel(slot);
			g_states[slot] = {};
		}
	}

	using namespace detail;

	void Invalidate() { for (auto& state : g_states) state.dirty = true; }

	const std::vector<UE::UObject*>& Evaluate(int side,
		const std::vector<UE::UObject*>& source, std::uint64_t generation)
	{
		const unsigned slot = side >= 3 && side <= 4 ? static_cast<unsigned>(side - 2) : 0;
		auto& state = g_states[slot];
		auto& primary = g_pickers[slot ? slot + 1 : 0];
		auto& second = g_pickers[1];
		const bool magic = side == 2;
		bool changed = false;
		if (state.generation != generation || state.menu != side) {
			geardata::Cancel(slot);
			state = {}; state.generation = generation; state.menu = side;
			std::vector<int> nativeIndices;
			std::vector<std::uint32_t> forms;
			for (auto* item : source) {
				state.indices.push_back(slot ? static_cast<int>(state.indices.size()) : Index(item));
				if (slot) {
					std::uint64_t key = 0;
					nativeIndices.push_back(Index(item)); forms.push_back(ContainerForm(item, &key)); state.keys.push_back(key);
					if (const auto known = key ? g_rowTags.find(key) : g_rowTags.end(); known != g_rowTags.end())
						state.masks[static_cast<int>(state.keys.size() - 1)] = known->second;
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
				if (slot && entry.index >= 0 && static_cast<std::size_t>(entry.index) < state.keys.size() && state.keys[entry.index]) {
					if (g_rowTags.size() > 16384) g_rowTags.clear();
					g_rowTags[state.keys[entry.index]] = entry.tags;
				}
			}
			state.ticket = state.dataDeadline = 0;
			changed = true;
		}
		if (state.ticket && GetTickCount64() > state.dataDeadline) {
			geardata::Cancel(slot); state.ticket = state.dataDeadline = 0;
			changed = true;
			logger::warn("Menus Revamped: item classification timed out; unresolved categories remain visible");
		}
		const bool useSecond = second.kind == 2;
		if (!changed && !state.dirty && primary.choice == primary.appliedChoice &&
			primary.category == primary.appliedCategory && primary.stolenOnly == primary.appliedStolen &&
			(!useSecond || second.choice == second.appliedChoice))
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
			if (mask == state.masks.end() || (primary.Passes(mask->second) && (!useSecond || second.Passes(mask->second))))
				state.filtered.push_back(item);
		}
		state.dirty = false;
		primary.appliedChoice = primary.choice; primary.appliedCategory = primary.category; primary.appliedStolen = primary.stolenOnly;
		second.appliedChoice = second.choice;
		return state.filtered;
	}
}
