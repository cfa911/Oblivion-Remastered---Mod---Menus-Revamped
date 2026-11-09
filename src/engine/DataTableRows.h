#pragma once
// Visits the rows of a UDataTable in memory (UE 5.3). RowMap is not reflected: a TMap<FName, uint8*> right after the
// reflected RowStruct. Its TSparseArray holds { element data ptr, Num, Max, TBitArray (4 inline dwords, secondary ptr,
// NumBits, MaxBits), FirstFreeIndex, NumFreeIndices }; each element is 24 bytes { FName, row data ptr, hash links }.
// Every read is checked with a_readable first, and an implausible layout is refused (-1).
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace datatablerows
{
	inline constexpr std::size_t kMapBytes = 56, kElement = 24;
	inline constexpr std::int32_t kMaxRows = 1024;

	// a_readable(ptr, bytes) -> bool; a_visit(std::uint64_t name, std::uint8_t* row). Returns the rows visited, or -1.
	template <class Readable, class Visit>
	int VisitRows(const std::uint8_t* a_map, Readable a_readable, Visit a_visit)
	{
		if (!a_map || !a_readable(a_map, kMapBytes)) return -1;
		const std::uint8_t* data = nullptr;
		std::int32_t num = 0;
		const std::uint32_t* secondary = nullptr;
		std::memcpy(&data, a_map, sizeof(data));
		std::memcpy(&num, a_map + 8, sizeof(num));
		std::memcpy(&secondary, a_map + 32, sizeof(secondary));
		if (num < 0 || num > kMaxRows) return -1;
		if (num == 0) return 0;
		if (!data || !a_readable(data, std::size_t(num) * kElement)) return -1;
		const auto* words = secondary ? secondary : reinterpret_cast<const std::uint32_t*>(a_map + 16);
		if (secondary ? !a_readable(secondary, std::size_t(num + 31) / 32 * 4) : num > 128) return -1;
		int visited = 0;
		for (std::int32_t i = 0; i < num; ++i) {
			if (!((words[i / 32] >> (i % 32)) & 1u)) continue;
			std::uint64_t name = 0;
			std::uint8_t* row = nullptr;
			std::memcpy(&name, data + std::size_t(i) * kElement, sizeof(name));
			std::memcpy(&row, data + std::size_t(i) * kElement + 8, sizeof(row));
			if (!row || !a_readable(row, 1)) continue;
			a_visit(name, row);
			++visited;
		}
		return visited;
	}
}
