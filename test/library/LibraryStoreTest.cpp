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
    for (int i = 0; i < 4; ++i) assert(store.setOrder(static_cast<LibraryStore::Order>(i)));
    std::cout << "501 books: " << store.indexBytes() << " bytes of catalog index\n";
  }
  std::filesystem::remove_all(LibraryFault::root);
  std::cout << "Library catalog scenarios passed\n";
}
