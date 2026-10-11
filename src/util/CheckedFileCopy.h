#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

// Copy a known-size file with a caller-owned bounded buffer. HAL reads are
// signed: check the result before conversion or use as a write length. Always
// close both files; failed writes/close must not be promoted over an original.
template <typename Source, typename Target, typename Yield>
bool checkedFileCopy(Source& source, Target& target, size_t expected, uint8_t* buffer, size_t capacity, Yield onChunk) {
  size_t copied = 0;
  if (buffer && capacity) {
    while (copied < expected) {
      const size_t requested = std::min(capacity, expected - copied);
      const auto readCount = source.read(buffer, requested);
      if (readCount <= 0 || static_cast<size_t>(readCount) > requested) break;
      const size_t count = static_cast<size_t>(readCount);
      if (target.write(buffer, count) != count) break;
      copied += count;
      onChunk();
    }
  }
  target.flush();
  const bool written = target.close();
  source.close();
  return buffer && capacity && copied == expected && written;
}
