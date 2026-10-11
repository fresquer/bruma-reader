#pragma once
#include <cstdint>
namespace MemoryBudget {
inline uint32_t freeHeap = 1024 * 1024, maxAllocHeap = 1024 * 1024;
struct HeapSnapshot {
  uint32_t freeHeap, maxAllocHeap;
};
inline HeapSnapshot snapshot() { return {freeHeap, maxAllocHeap}; }
inline bool hasHeap(HeapSnapshot heap, uint32_t free, uint32_t contiguous) {
  return heap.freeHeap >= free && heap.maxAllocHeap >= contiguous;
}
}  // namespace MemoryBudget
