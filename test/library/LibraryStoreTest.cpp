#include <MemoryBudget.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

#include "LibraryStore.h"
#include "RecentBooksStore.h"

RecentBooksStore RecentBooksStore::instance;
void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                               const std::string&, const std::string&) {
  recentBooks.insert(recentBooks.begin(), {"", path, title, author, ""});
}
namespace JsonSettingsIO {
bool loadRecentBooks(RecentBooksStore& store, const char*) {
  store.recentBooks.clear();
  return true;
}
}  // namespace JsonSettingsIO
static int metadataReads = 0;
bool readLibraryMetadata(const std::string& path, std::string& title, std::string& author) {
  ++metadataReads;
  std::ifstream file(LibraryFault::root + path);
  std::getline(file, title, '|');
  std::getline(file, author);
  if (title == "BROKEN") {
    title.clear();
    author.clear();
    return false;
  }
  return true;
}

static void put(const std::string& path, const std::string& content = "") {
  std::filesystem::create_directories(std::filesystem::path(LibraryFault::root + path).parent_path());
  std::ofstream(LibraryFault::root + path) << content;
}
static std::string bytes(const std::string& path) {
  std::ifstream f(LibraryFault::root + path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}
static void fresh() {
  std::filesystem::remove_all(LibraryFault::root);
  std::filesystem::create_directories(LibraryFault::root);
  LibraryFault::reset();
  metadataReads = 0;
  MemoryBudget::freeHeap = MemoryBudget::maxAllocHeap = 1024 * 1024;
  JsonSettingsIO::loadRecentBooks(RECENT_BOOKS, "");
}
static LibraryStore::ScanState scan(LibraryStore& store) {
  assert(store.beginScan());
  for (int i = 0; i < 20000; ++i) {
    const auto state = store.step();
    if (state == LibraryStore::ScanState::Complete || state == LibraryStore::ScanState::Failed) return state;
  }
  assert(false && "scan did not terminate");
  return LibraryStore::ScanState::Failed;
}
static LibraryStore::Book book(LibraryStore& store, unsigned i) {
  LibraryStore::Book b;
  assert(store.readBook(i, b));
  return b;
}

using State = LibraryStore::ReadingState;
using Filter = LibraryStore::Filter;
static std::map<std::string, State> states;
static State resolveState(const LibraryStore::Book& book, void*) {
  const auto it = states.find(book.path);
  return it != states.end() ? it->second : book.opened ? State::Reading : State::Pending;
}
static LibraryStore::Book visibleBook(LibraryStore& store, unsigned i) {
  LibraryStore::Book b;
  assert(store.readVisibleBook(i, b));
  return b;
}

int main() {
  LibraryFault::root =
      (std::filesystem::temp_directory_path() /
       ("bruma-library-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
          .string();
  fresh();
  {
    LibraryStore store;
    assert(!store.load(LibraryStore::Order::Title));
    put("/A/one.EPUB", "Book 10|Zulu");
    put("/B/Nested/two.xtch", "Book 2|Alpha");
    put("/notes.MD", "|No author");
    put("/bad.epub", "BROKEN");
    put("/text.txt", "Plain text");
    put("/.hidden/skip.epub", "Hidden");
    put("/System Volume Information/skip.epub", "Hidden");
    put("/covers/cover.png", "Image");
    put("/system volume information/skip.epub", "Hidden");
    put("/$recycle.bin/skip.epub", "Hidden");
    put("/firmware.bin", "Firmware");
    assert(scan(store) == LibraryStore::ScanState::Complete);
    assert(store.count() == 5);
    assert(book(store, 1).title == "Book 2");
    assert(book(store, 2).title == "Book 10");
    const int reads = metadataReads;
    assert(scan(store) == LibraryStore::ScanState::Complete);
    assert(metadataReads == reads);
    put("/A/one.EPUB", "Changed title|Zulu");
    assert(scan(store) == LibraryStore::ScanState::Complete);
    assert(metadataReads == reads + 1);
    // Same-size edits are detected from modification time, not just file size.
    put("/A/one.EPUB", "Updated title|Zulu");
    auto edited = std::filesystem::path(LibraryFault::root + "/A/one.EPUB");
    std::filesystem::last_write_time(edited, std::filesystem::last_write_time(edited) + std::chrono::seconds(2));
    assert(scan(store) == LibraryStore::ScanState::Complete);
    assert(metadataReads == reads + 2);
    assert(store.setOrder(LibraryStore::Order::Author));
    assert(book(store, 0).author == "Alpha");
    // New batch precedes the original collection, even when alphabetically last.
    put("/new.epub", "Zzz new|Author");
    assert(scan(store) == LibraryStore::ScanState::Complete);
    assert(store.setOrder(LibraryStore::Order::Added));
    assert(book(store, 0).path == "/new.epub");
    assert(LibraryStore::recordOpened("/text.txt"));
    assert(LibraryStore::recordOpened("/A/one.EPUB"));
    assert(store.load(LibraryStore::Order::LastRead));
    assert(book(store, 0).path == "/A/one.EPUB");
    assert(book(store, 1).path == "/text.txt");
    std::filesystem::remove(LibraryFault::root + "/text.txt");
    assert(scan(store) == LibraryStore::ScanState::Complete);
    assert(store.count() == 5);
    const auto good = bytes("/.crosspoint/library.bin");
    // Cancel never publishes partial discoveries or removes existing records.
    put("/cancelled.epub", "Cancelled");
    assert(store.beginScan());
    store.step();
    store.cancel();
    assert(bytes("/.crosspoint/library.bin") == good);
    assert(store.count() == 5);
    // Write, directory enumeration, and source read errors keep the old catalog.
    assert(store.beginScan());
    LibraryFault::failWrite = "library.bin.tmp";
    while (store.step() == LibraryStore::ScanState::Scanning) {
    }
    assert(store.scanState() == LibraryStore::ScanState::Failed);
    assert(bytes("/.crosspoint/library.bin") == good);
    LibraryFault::reset();
    assert(store.beginScan());
    LibraryFault::failDirectory = "/B";
    while (store.step() == LibraryStore::ScanState::Scanning) {
    }
    assert(store.scanState() == LibraryStore::ScanState::Failed);
    assert(bytes("/.crosspoint/library.bin") == good);
    LibraryFault::reset();
    LibraryFault::failRead = "/cancelled.epub";
    LibraryFault::readsUntilFailure = 0;
    assert(scan(store) == LibraryStore::ScanState::Failed);
    assert(bytes("/.crosspoint/library.bin") == good);
    LibraryFault::reset();
    LibraryFault::failRead = "library.bin";
    LibraryFault::readsUntilFailure = 0;
    assert(!store.setOrder(LibraryStore::Order::Author));
    assert(bytes("/.crosspoint/library.bin") == good);
    LibraryFault::reset();
    assert(store.load(LibraryStore::Order::LastRead));
    // A partial history append is repaired before the next successful opening.
    std::ofstream(LibraryFault::root + "/.crosspoint/library-open.bin", std::ios::app) << "partial";
    assert(LibraryStore::recordOpened("/new.epub"));
    assert(store.load(LibraryStore::Order::LastRead));
    assert(book(store, 0).path == "/new.epub");
    // Low-memory discovery is a clean failure, not a truncated published library.
    MemoryBudget::freeHeap = MemoryBudget::maxAllocHeap = 16000;
    assert(!store.beginScan());
    assert(bytes("/.crosspoint/library.bin") == good);
    MemoryBudget::freeHeap = MemoryBudget::maxAllocHeap = 1024 * 1024;
    // MAIN missing between generation renames recovers BACKUP, ignoring TEMP.
    store.release();
    std::filesystem::copy_file(LibraryFault::root + "/.crosspoint/library.bin",
                               LibraryFault::root + "/.crosspoint/library.bin.bak",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove(LibraryFault::root + "/.crosspoint/library.bin");
    put("/.crosspoint/library.bin.tmp", "uncommitted");
    assert(store.load(LibraryStore::Order::Title));
    assert(store.count() == 5);
    assert(scan(store) == LibraryStore::ScanState::Complete);
    // Corrupt MAIN checksum recovers the previous valid generation.
    store.release();
    put("/.crosspoint/library.bin", "corrupt");
    assert(store.load(LibraryStore::Order::Title));
    assert(store.count() == 5);
  }
  fresh();
  {
    RECENT_BOOKS.addBook("/old.epub", "Old", "", "", "");
    RECENT_BOOKS.addBook("/latest.epub", "Latest", "", "", "");
    put("/old.epub", "Old");
    put("/latest.epub", "Latest");
    put("/unread.epub", "Unread");
    LibraryStore store;
    assert(scan(store) == LibraryStore::ScanState::Complete);
    assert(store.setOrder(LibraryStore::Order::LastRead));
    assert(book(store, 0).path == "/latest.epub");
    assert(book(store, 1).path == "/old.epub");
    assert(book(store, 2).opened == 0);
  }
  fresh();
  {
    put("/undated.epub", "Undated|Author");
    LibraryFault::missingTimestamp = "/undated.epub";
    LibraryStore store;
    assert(scan(store) == LibraryStore::ScanState::Complete);
    assert(store.count() == 1);
    assert(metadataReads == 1);
    assert(scan(store) == LibraryStore::ScanState::Complete);
    assert(metadataReads == 2);  // Missing FAT date is safe, but cannot reuse metadata.
  }
  fresh();
  {
    std::string deepPath;
    for (int i = 0; i < 64; ++i) deepPath += "/d";
    deepPath += "/deep.epub";
    put(deepPath, "Deep|Author");
    LibraryStore store;
    assert(store.beginScan());
    for (int i = 0; i < 80; ++i) assert(store.step() == LibraryStore::ScanState::Scanning);
    store.cancel();  // Release a chain of live parent handles without stack recursion.
    assert(!Storage.exists("/.crosspoint/library.bin"));
    assert(!Storage.exists("/.crosspoint/library.bin.tmp"));
    assert(scan(store) == LibraryStore::ScanState::Complete);
    assert(store.count() == 1);
    assert(book(store, 0).path == deepPath);
  }
  fresh();
  {
    for (int i = 0; i < 500; ++i)
      put("/shelf" + std::to_string(i % 7) + "/Book" + std::to_string(i) + ".epub",
          "Book " + std::to_string(i) + "|Author " + std::to_string(i % 20));
    put("/copy/duplicate.epub", "Book 2|Author 2");
    LibraryStore store;
    assert(scan(store) == LibraryStore::ScanState::Complete);
    assert(store.count() == 501);
    assert(store.indexBytes() == 512 * 20);
    assert(store.indexBytes() < 11000);
    assert(book(store, 0).title == "Book 0");
    assert(book(store, 500).title == "Book 499");
    int duplicates = 0;
    for (unsigned i = 0; i < store.count(); ++i)
      if (book(store, i).duplicate) ++duplicates;
    assert(duplicates == 2);
    assert(store.refreshReadingStates([](const LibraryStore::Book& b, void*) {
      const auto number = std::stoi(b.title.substr(5));
      return static_cast<LibraryStore::ReadingState>(1 + number % 3);
    }));
    for (int filter = 1; filter < 4; ++filter) {
      store.setFilter(static_cast<LibraryStore::Filter>(filter));
      assert(store.visibleCount() == 167);
      LibraryStore::Book last;
      assert(store.readVisibleBook(166, last));
      assert(!store.readVisibleBook(167, last));
      assert(store.indexBytes() == 512 * 20);
    }
    store.setFilter(LibraryStore::Filter::All);
    assert(store.visibleCount() == 501);
    for (int i = 0; i < 4; ++i) assert(store.setOrder(static_cast<LibraryStore::Order>(i)));
    std::cout << "501 books: " << store.indexBytes() << " bytes of catalog index\n";
  }
  fresh();
  {
    put("/a.epub", "Alpha|Author");
    put("/b.epub", "Beta|Author");
    put("/c.epub", "Gamma|Author");
    put("/copy.epub", "Alpha|Author");
    LibraryStore store;
    assert(scan(store) == LibraryStore::ScanState::Complete);
    assert(LibraryStore::recordOpened("/a.epub"));
    assert(store.load(LibraryStore::Order::Title));
    states = {{"/c.epub", State::Finished}};
    assert(store.refreshReadingStates(resolveState));
    const auto indexBytes = store.indexBytes();
    const auto catalog = bytes("/.crosspoint/library.bin");
    store.setFilter(Filter::All);
    assert(store.visibleCount() == 4);
    store.setFilter(Filter::Reading);
    assert(store.visibleCount() == 1 && visibleBook(store, 0).path == "/a.epub");
    assert(store.findVisiblePath("/a.epub") == 0);
    assert(store.findVisiblePath("/b.epub") == -1);
    store.setFilter(Filter::Pending);
    assert(store.visibleCount() == 2);  // An unopened copy has its own state.
    for (int order = 0; order < 4; ++order) {
      assert(store.setOrder(static_cast<LibraryStore::Order>(order)));
      assert(store.visibleCount() == 2);
      assert(visibleBook(store, 0).readingState == State::Pending);
    }
    store.setFilter(Filter::Finished);
    assert(store.visibleCount() == 1 && visibleBook(store, 0).path == "/c.epub");
    assert(store.setReadingState("/b.epub", State::Finished));
    assert(store.visibleCount() == 2);
    assert(bytes("/.crosspoint/library.bin") == catalog);
    assert(store.indexBytes() == indexBytes);
    assert(store.load(LibraryStore::Order::Title));
    assert(store.refreshReadingStates(resolveState));
    store.setFilter(Filter::Finished);
    assert(store.visibleCount() == 2 && visibleBook(store, 0).manualState);
    assert(store.setReadingState("/b.epub", State::Pending));
    store.setFilter(Filter::Pending);
    assert(store.visibleCount() == 2);
    assert(LibraryStore::recordOpened("/b.epub"));  // Pending -> Reading automatically on open.
    assert(store.load(LibraryStore::Order::Title));
    assert(store.refreshReadingStates(resolveState));
    store.setFilter(Filter::Reading);
    assert(store.visibleCount() == 2);
    assert(store.setReadingState("/c.epub", State::Reading));  // Undo completion without changing progress/stats.
    assert(store.visibleCount() == 3);
    assert(LibraryStore::recordFinished("/c.epub"));
    assert(store.refreshReadingStates(resolveState));
    store.setFilter(Filter::Finished);
    assert(store.visibleCount() == 1 && !visibleBook(store, 0).manualState);
    // Incomplete append retains the last complete state and is repaired on retry.
    assert(store.setReadingState("/b.epub", State::Finished));
    std::ofstream(LibraryFault::root + "/.crosspoint/library-state.bin", std::ios::app) << "torn";
    assert(store.refreshReadingStates(resolveState));
    assert(store.visibleCount() == 2);
    assert(store.setReadingState("/b.epub", State::Reading));
    assert(store.refreshReadingStates(resolveState));
    assert(store.visibleCount() == 1);
    // Failed writes and IO reads cannot change the current in-memory state or trim history.
    const auto saved = bytes("/.crosspoint/library-state.bin");
    LibraryFault::failWrite = "library-state.bin";
    assert(!store.setReadingState("/b.epub", State::Finished));
    assert(bytes("/.crosspoint/library-state.bin") == saved);
    LibraryFault::reset();
    LibraryFault::failRead = "library-state.bin";
    LibraryFault::readsUntilFailure = 0;
    assert(!store.setReadingState("/b.epub", State::Finished));
    assert(bytes("/.crosspoint/library-state.bin") == saved);
    LibraryFault::reset();
    assert(store.refreshReadingStates(resolveState));
    MemoryBudget::freeHeap = 16000;
    assert(!store.setReadingState("/b.epub", State::Finished));
    assert(bytes("/.crosspoint/library-state.bin") == saved);
    MemoryBudget::freeHeap = 1024 * 1024;
    // A recovered backup remains usable after a crash between renames.
    std::filesystem::rename(LibraryFault::root + "/.crosspoint/library-state.bin",
                            LibraryFault::root + "/.crosspoint/library-state.bin.bak.recovered");
    std::filesystem::remove(LibraryFault::root + "/.crosspoint/library-state.bin.bak");
    std::filesystem::rename(LibraryFault::root + "/.crosspoint/library-state.bin.bak.recovered",
                            LibraryFault::root + "/.crosspoint/library-state.bin.bak");
    assert(store.refreshReadingStates(resolveState));
    assert(store.setReadingState("/b.epub", State::Finished));
    assert(store.refreshReadingStates(resolveState));
    assert(store.visibleCount() == 2);
    assert(scan(store) == LibraryStore::ScanState::Complete);
    assert(store.refreshReadingStates(resolveState));
    assert(store.visibleCount() == 2);  // Manual states survive catalog refresh.
    assert(store.setReadingState("/b.epub", State::Reading));
    assert(store.setReadingState("/c.epub", State::Reading));
    assert(store.visibleCount() == 0);
    store.setFilter(Filter::All);
    assert(store.visibleCount() == 4);  // Empty view never loses the catalog.
    assert(!store.setReadingState("/absent.epub", State::Finished));
  }
  std::filesystem::remove_all(LibraryFault::root);
  std::cout << "Library catalog scenarios passed\n";
}
