#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "util/CheckedFileCopy.h"

namespace {
struct Source {
  std::vector<int> results;
  std::vector<size_t> requested;
  size_t next = 0;
  int closed = 0;
  Source() = default;
  explicit Source(std::initializer_list<int> values) : results(values) {}
  int read(uint8_t* out, size_t count) {
    requested.push_back(count);
    const int result = next < results.size() ? results[next++] : 0;
    if (result > 0) std::memset(out, 'a', std::min(count, static_cast<size_t>(result)));
    return result;
  }
  bool close() {
    ++closed;
    return true;
  }
};
struct Target {
  std::string bytes;
  std::vector<size_t> written;
  bool shortWrite = false;
  bool closeOk = true;
  int flushed = 0;
  int closed = 0;
  size_t write(const uint8_t* data, size_t count) {
    written.push_back(count);
    const size_t result = shortWrite ? count - 1 : count;
    bytes.append(reinterpret_cast<const char*>(data), result);
    return result;
  }
  void flush() { ++flushed; }
  bool close() {
    ++closed;
    return closeOk;
  }
};
void expectClosed(const Source& source, const Target& target) {
  EXPECT_EQ(source.closed, 1);
  EXPECT_EQ(target.closed, 1);
  EXPECT_EQ(target.flushed, 1);
}
}  // namespace

TEST(CheckedFileCopy, CopiesExactLengthWithBoundedFinalReadAndYieldPerChunk) {
  Source source{4, 4, 2};
  Target target;
  std::array<uint8_t, 4> buffer{};
  int yields = 0;
  EXPECT_TRUE(checkedFileCopy(source, target, 10, buffer.data(), buffer.size(), [&] { ++yields; }));
  EXPECT_EQ(source.requested, (std::vector<size_t>{4, 4, 2}));
  EXPECT_EQ(target.written, source.requested);
  EXPECT_EQ(target.bytes, std::string(10, 'a'));
  EXPECT_EQ(yields, 3);
  expectClosed(source, target);
}

TEST(CheckedFileCopy, HandlesShortReadsUntilExpectedSizeIsCopied) {
  Source source{2, 1, 3};
  Target target;
  uint8_t buffer[4]{};
  int yields = 0;
  EXPECT_TRUE(checkedFileCopy(source, target, 6, buffer, sizeof(buffer), [&] { ++yields; }));
  EXPECT_EQ(source.requested, (std::vector<size_t>{4, 4, 3}));
  EXPECT_EQ(target.written, (std::vector<size_t>{2, 1, 3}));
  EXPECT_EQ(target.bytes.size(), 6u);
  EXPECT_EQ(yields, 3);
  expectClosed(source, target);
}

TEST(CheckedFileCopy, NeverWritesNegativeZeroOrOversizedReadCounts) {
  for (const int result : {-1, 0, 5}) {
    Source source{result};
    Target target;
    uint8_t buffer[4]{};
    int yields = 0;
    EXPECT_FALSE(checkedFileCopy(source, target, 4, buffer, sizeof(buffer), [&] { ++yields; }));
    EXPECT_TRUE(target.written.empty());
    EXPECT_EQ(yields, 0);
    expectClosed(source, target);
  }
}

TEST(CheckedFileCopy, RejectsOversizedFinalReadAfterValidChunk) {
  Source source{4, 3};
  Target target;
  uint8_t buffer[4]{};
  int yields = 0;
  EXPECT_FALSE(checkedFileCopy(source, target, 6, buffer, sizeof(buffer), [&] { ++yields; }));
  EXPECT_EQ(source.requested, (std::vector<size_t>{4, 2}));
  EXPECT_EQ(target.written, (std::vector<size_t>{4}));
  EXPECT_EQ(yields, 1);
  expectClosed(source, target);
}

TEST(CheckedFileCopy, RejectsEarlyReadFailureAndClosesBothFiles) {
  for (const int result : {-1, 0}) {
    Source source{2, result};
    Target target;
    uint8_t buffer[4]{};
    int yields = 0;
    EXPECT_FALSE(checkedFileCopy(source, target, 6, buffer, sizeof(buffer), [&] { ++yields; }));
    EXPECT_EQ(target.written, (std::vector<size_t>{2}));
    EXPECT_EQ(yields, 1);
    expectClosed(source, target);
  }
}

TEST(CheckedFileCopy, RejectsShortWriteWithoutFurtherReadsOrYield) {
  Source source{4, 4};
  Target target;
  target.shortWrite = true;
  uint8_t buffer[4]{};
  int yields = 0;
  EXPECT_FALSE(checkedFileCopy(source, target, 8, buffer, sizeof(buffer), [&] { ++yields; }));
  EXPECT_EQ(source.requested.size(), 1u);
  EXPECT_EQ(target.written, (std::vector<size_t>{4}));
  EXPECT_EQ(yields, 0);
  expectClosed(source, target);
}

TEST(CheckedFileCopy, RejectsTargetCloseFailureAfterSuccessfulCopy) {
  Source source{4};
  Target target;
  target.closeOk = false;
  uint8_t buffer[4]{};
  EXPECT_FALSE(checkedFileCopy(source, target, 4, buffer, sizeof(buffer), [] {}));
  EXPECT_EQ(target.bytes.size(), 4u);
  expectClosed(source, target);
}

TEST(CheckedFileCopy, RejectsInvalidBufferWithoutReadingAndAcceptsEmptyFile) {
  for (const bool nullBuffer : {false, true}) {
    Source source{4};
    Target target;
    uint8_t buffer[4]{};
    EXPECT_FALSE(
        checkedFileCopy(source, target, 4, nullBuffer ? nullptr : buffer, nullBuffer ? sizeof(buffer) : 0, [] {}));
    EXPECT_TRUE(source.requested.empty());
    EXPECT_TRUE(target.written.empty());
    expectClosed(source, target);
  }
  Source source;
  Target target;
  uint8_t buffer[4]{};
  EXPECT_TRUE(checkedFileCopy(source, target, 0, buffer, sizeof(buffer), [] {}));
  EXPECT_TRUE(source.requested.empty());
  EXPECT_TRUE(target.written.empty());
  expectClosed(source, target);
}
