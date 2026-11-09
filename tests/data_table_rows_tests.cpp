// Perk emphasis (owner batch 2026-11-09): the brown highlighted words come from rows of a rich-text style table shared
// with the Stats menu. The Inventory restyles them by visiting that table's rows in memory (UDataTable::RowMap, a
// TMap<FName, uint8*> right after RowStruct), so the walk must find exactly the allocated rows and refuse a bad layout.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#if __has_include("../src/engine/DataTableRows.h")
#include "../src/engine/DataTableRows.h"
#else
// Pre-feature behaviour: no rows are visited, so the emphasis keeps the table's brown.
namespace datatablerows {
template <class Readable, class Visit> int VisitRows(const std::uint8_t*, Readable, Visit) { return -1; }
}
#endif
unsigned checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}

// A fake UE 5.3 TMap<FName,uint8*>: TSparseArray { Data ptr, Num, Max, TBitArray { 4 inline dwords, secondary ptr,
// NumBits, MaxBits }, FirstFreeIndex, NumFreeIndices }; elements of 24 bytes { FName (8), value (8), hash next/index (8) }.
struct Element { std::uint64_t name; std::uint8_t* value; std::int32_t next, index; };
struct FakeMap { Element* data; std::int32_t num, max; std::uint32_t bits[4]; void* secondary; std::int32_t numBits, maxBits, firstFree, numFree; };

int main(){
 using namespace datatablerows;
 static std::uint8_t rows[4][32]{};
 std::vector<Element> elems{{11,rows[0],0,0},{12,rows[1],0,0},{13,nullptr,0,0},{14,rows[3],0,0}};
 FakeMap map{elems.data(),4,4,{0b1011u,0,0,0},nullptr,4,128,2,1};
 const auto readable=[](const void* p,std::size_t){return p!=nullptr;};
 std::vector<std::uint64_t> names; std::vector<std::uint8_t*> values;
 const int n=VisitRows(reinterpret_cast<const std::uint8_t*>(&map),readable,[&](std::uint64_t name,std::uint8_t* row){names.push_back(name);values.push_back(row);});
 Check(n==3,"visits the three allocated rows");
 Check(names==std::vector<std::uint64_t>{11,12,14},"skips the freed slot and keeps the order");
 Check(values.size()==3&&values[0]==rows[0]&&values[2]==rows[3],"hands each row's data");
 FakeMap broken=map; broken.num=5000;
 Check(VisitRows(reinterpret_cast<const std::uint8_t*>(&broken),readable,[&](std::uint64_t,std::uint8_t*){})==-1,"an implausible row count is refused");
 FakeMap empty=map; empty.data=nullptr;
 Check(VisitRows(reinterpret_cast<const std::uint8_t*>(&empty),readable,[&](std::uint64_t,std::uint8_t*){})==-1,"unreadable row storage is refused");
 std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
