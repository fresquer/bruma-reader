#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
namespace MemoryBudget {
inline uint32_t freeHeap = 1024 * 1024, maxAllocHeap = 1024 * 1024;
inline size_t snapshotsBeforeLow = std::numeric_limits<size_t>::max();
struct HeapSnapshot {
  uint32_t freeHeap, maxAllocHeap;
};
inline HeapSnapshot snapshot() {
  if (snapshotsBeforeLow != std::numeric_limits<size_t>::max()) {
    if (!snapshotsBeforeLow) return {16000, 16000};
    --snapshotsBeforeLow;
  }
  return {freeHeap, maxAllocHeap};
}
inline bool hasHeap(HeapSnapshot heap, uint32_t free, uint32_t contiguous) {
  return heap.freeHeap >= free && heap.maxAllocHeap >= contiguous;
}
}  // namespace MemoryBudget
