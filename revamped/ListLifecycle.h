#pragma once
#include <cstdint>
#include <vector>

namespace revamped
{
	// The visible filtered list must never replace the complete native source.
	// A native rebuild is a generation even when count and wrapper pointers match.
	template<class Row>
	struct ListLifecycle
	{
		std::vector<Row> source, applied;
		std::uint64_t generation = 0;
		bool nativeDirty = true, haveSource = false;

		bool Observe(const std::vector<Row>& live)
		{
			if (haveSource && !nativeDirty && live == applied) return false;
			source = live;
			nativeDirty = false;
			haveSource = true;
			++generation;
			return true;
		}

		void Commit(const std::vector<Row>& result) { applied = result; }
		void Reset() { *this = {}; }
	};
}
