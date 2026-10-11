#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <memory>
#include <string>

// The catalog owns no resident title/path collection. Only record offsets and
// order keys live in RAM; callers load a bounded page from the SD card.
class LibraryStore {
 public:
  enum class Order : uint8_t { Title, Author, LastRead, Added };
  enum class Filter : uint8_t { All, Reading, Pending, Finished };
  enum class ReadingState : uint8_t { Automatic, Pending, Reading, Finished };
  enum class ScanState : uint8_t { Idle, Scanning, Publishing, Complete, Failed, Cancelled };
  struct Book {
    std::string path, title, author;
    uint64_t size = 0, modified = 0;
    uint32_t added = 0, opened = 0;
    bool duplicate = false;
    ReadingState readingState = ReadingState::Pending;
    bool manualState = false;
  };
  LibraryStore() = default;
  ~LibraryStore();
  bool load(Order order);
  void release();
  uint32_t count() const { return used; }
  bool exists() const { return loaded; }
  bool readBook(uint32_t index, Book& book);
  using StateResolver = ReadingState (*)(const Book&, void*);
  bool refreshReadingStates(StateResolver resolver, void* context = nullptr);
  void setFilter(Filter filter) { filtering = filter; }
  uint32_t visibleCount() const;
  bool readVisibleBook(uint32_t index, Book& book);
  int findVisiblePath(const std::string& path);
  bool setReadingState(const std::string& path, ReadingState state);
  // Clear a manual override only when the reader actually finishes the book.
  static bool recordFinished(const std::string& path);
  bool setOrder(Order order);
  int findPath(const std::string& path);
  bool beginScan();
  ScanState step();  // At most one SD directory entry / metadata extraction.
  void cancel();
  ScanState scanState() const { return state; }
  uint32_t found() const { return foundBooks; }
  size_t indexBytes() const { return capacity * sizeof(Index); }
  static bool recordOpened(const std::string& path);

 private:
  struct Index {
    uint32_t offset, hash, added, opened;
    bool duplicate;
    ReadingState readingState = ReadingState::Pending;
    ReadingState manualState = ReadingState::Automatic;
    ReadingState automaticState = ReadingState::Pending;
  };
  struct Directory {
    HalFile file;
    std::string path;
    std::unique_ptr<Directory> parent;
  };
  std::unique_ptr<Index[]> records;
  uint32_t used = 0, capacity = 0, batch = 0, sequence = 0;
  bool loaded = false, readFailed = false;
  Order ordering = Order::Title;
  Filter filtering = Filter::All;
  ScanState state = ScanState::Idle;
  HalFile catalog, output, sourceCheck;
  std::unique_ptr<Book> pendingBook;
  std::unique_ptr<Directory> directory;
  std::unique_ptr<char[]> nameBuffer;
  uint32_t foundBooks = 0;
  std::string activePath;
  void closeDirectories();
  bool loadFile(const char* path);
  bool appendIndex(const Index& entry);
  bool readAt(uint32_t offset, Book& book);
  bool applyHistory();
  bool markDuplicates();
  bool publish();
  bool matches(const Index& entry) const;
  bool applyReadingStates();
  ScanState fail();
};
