#pragma once

namespace geardata
{
	struct Entry { int index; std::uint32_t tags; bool valid; };  // tags: see ItemTags.h
	// UE callers exchange only value data with the verified native TES callback.
	std::uint64_t Request(std::vector<int> a_indices, std::vector<std::uint32_t> a_forms = {}, unsigned a_slot = 0, std::vector<int> a_nativeIndices = {});
	bool Take(std::uint64_t a_ticket, std::vector<Entry>& a_entries);
	void Cancel(unsigned a_slot = 0);
}
