#include "LibraryStore.h"

#include <FsHelpers.h>
#include <Logging.h>
#include <MemoryBudget.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <new>

#include "LibraryMetadata.h"
#include "RecentBooksStore.h"
#ifdef SIMULATOR
#include <sys/stat.h>

#include <cerrno>
#include <cstdlib>
#endif

namespace {
constexpr const char* MAIN = "/.crosspoint/library.bin";
constexpr const char* BACKUP = "/.crosspoint/library.bin.bak";
constexpr const char* TEMP = "/.crosspoint/library.bin.tmp";
constexpr const char* HISTORY = "/.crosspoint/library-open.bin";
constexpr const char* STATES = "/.crosspoint/library-state.bin";
constexpr const char* STATES_BACKUP = "/.crosspoint/library-state.bin.bak";
constexpr const char* STATES_TEMP = "/.crosspoint/library-state.bin.tmp";
constexpr uint32_t MAGIC = 0x42524c42;  // BRLB, little endian on both targets
constexpr uint32_t VERSION = 1;
constexpr size_t HEADER_SIZE = 8 * sizeof(uint32_t);
constexpr size_t NAME_SIZE = 1024;
constexpr uint32_t MAX_PATH = 2048, MAX_TITLE = 512, MAX_AUTHOR = 256;

uint32_t hashBytes(uint32_t hash, const void* bytes, size_t count) {
  const auto* p = static_cast<const uint8_t*>(bytes);
  for (size_t i = 0; i < count; ++i) hash = (hash ^ p[i]) * 16777619U;
  return hash;
}
uint32_t pathHash(const std::string& path) { return hashBytes(2166136261U, path.data(), path.size()); }
bool readExact(HalFile& file, void* data, size_t count) { return file.read(data, count) == static_cast<int>(count); }
bool writeExact(HalFile& file, const void* data, size_t count) { return file.write(data, count) == count; }

bool header(HalFile& file, uint32_t& count, uint32_t& batch, uint32_t& sequence) {
  uint32_t data[8];
  if (!file || !file.seek(0) || !readExact(file, data, sizeof(data))) return false;
  if (data[0] != MAGIC || data[1] != VERSION || data[7] != hashBytes(2166136261U, data, 28)) return false;
  count = data[2];
  batch = data[3];
  sequence = data[4];
  return file.size() >= HEADER_SIZE && count <= (file.size() - HEADER_SIZE) / 44;
}
bool writeHeader(HalFile& file, uint32_t count, uint32_t batch, uint32_t sequence) {
  uint32_t data[8] = {MAGIC, VERSION, count, batch, sequence, 0, 0, 0};
  data[7] = hashBytes(2166136261U, data, 28);
  return file.seek(0) && writeExact(file, data, sizeof(data));
}

bool readRecord(HalFile& file, LibraryStore::Book& book, bool* resourceFailure = nullptr) {
  if (resourceFailure) *resourceFailure = false;
  uint32_t data[10];
  if (!readExact(file, data, sizeof(data))) return false;
  if (!data[0] || data[0] > MAX_PATH || data[1] > MAX_TITLE || data[2] > MAX_AUTHOR) return false;
  const size_t length = data[0] + data[1] + data[2];
  if (file.position() > file.size() || length + 4 > file.size() - file.position()) return false;
  if (!MemoryBudget::hasHeap(MemoryBudget::snapshot(), static_cast<uint32_t>(length + 16 * 1024),
                             std::max({data[0], data[1], data[2]}) * 2 + 32)) {
    if (resourceFailure) *resourceFailure = true;
    return false;
  }
  book.path.resize(data[0]);
  book.title.resize(data[1]);
  book.author.resize(data[2]);
  if (!readExact(file, &book.path[0], data[0]) || (data[1] && !readExact(file, &book.title[0], data[1])) ||
      (data[2] && !readExact(file, &book.author[0], data[2])))
    return false;
  uint32_t checksum;
  uint32_t hash = hashBytes(2166136261U, data, sizeof(data));
  hash = hashBytes(hash, book.path.data(), book.path.size());
  hash = hashBytes(hash, book.title.data(), book.title.size());
  hash = hashBytes(hash, book.author.data(), book.author.size());
  if (!readExact(file, &checksum, sizeof(checksum)) || checksum != hash || book.path.front() != '/' ||
      book.path.find('\0') != std::string::npos || book.title.find('\0') != std::string::npos ||
      book.author.find('\0') != std::string::npos)
    return false;
  book.size = data[3] | (static_cast<uint64_t>(data[4]) << 32);
  book.modified = data[5] | (static_cast<uint64_t>(data[6]) << 32);
  book.added = data[7];
  book.opened = data[8];
  book.duplicate = false;
  return true;
}
bool writeRecord(HalFile& file, const LibraryStore::Book& book) {
  if (book.path.empty() || book.path.size() > MAX_PATH || book.title.size() > MAX_TITLE ||
      book.author.size() > MAX_AUTHOR)
    return false;
  const uint32_t data[10] = {static_cast<uint32_t>(book.path.size()),
                             static_cast<uint32_t>(book.title.size()),
                             static_cast<uint32_t>(book.author.size()),
                             static_cast<uint32_t>(book.size),
                             static_cast<uint32_t>(book.size >> 32),
                             static_cast<uint32_t>(book.modified),
                             static_cast<uint32_t>(book.modified >> 32),
                             book.added,
                             book.opened,
                             0};
  uint32_t hash = hashBytes(2166136261U, data, sizeof(data));
  hash = hashBytes(hash, book.path.data(), book.path.size());
  hash = hashBytes(hash, book.title.data(), book.title.size());
  hash = hashBytes(hash, book.author.data(), book.author.size());
  return writeExact(file, data, sizeof(data)) && writeExact(file, book.path.data(), book.path.size()) &&
         writeExact(file, book.title.data(), book.title.size()) &&
         writeExact(file, book.author.data(), book.author.size()) && writeExact(file, &hash, sizeof(hash));
}

bool supported(const std::string& name) {
  return FsHelpers::hasEpubExtension(name) || FsHelpers::hasXtcExtension(name) || FsHelpers::hasTxtExtension(name) ||
         FsHelpers::hasMarkdownExtension(name);
}
bool excluded(const std::string& name) {
  if (name.empty() || name[0] == '.') return true;
  for (const char* system : {"System Volume Information", "$RECYCLE.BIN"}) {
    if (name.size() == strlen(system) &&
        std::equal(name.begin(), name.end(), system,
                   [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }))
      return true;
  }
  return false;
}
std::string fallbackTitle(const std::string& path) {
  auto title = path.substr(path.find_last_of('/') + 1);
  const auto dot = title.find_last_of('.');
  if (dot != std::string::npos) title.resize(dot);
  if (title.size() > MAX_TITLE) {
    size_t cut = MAX_TITLE;
    while (cut && (static_cast<unsigned char>(title[cut]) & 0xc0) == 0x80) --cut;
    title.resize(cut);
  }
  return title;
}
bool heapAvailable(size_t bytes = 0) {
  return MemoryBudget::hasHeap(MemoryBudget::snapshot(), static_cast<uint32_t>(bytes + 24 * 1024),
                               static_cast<uint32_t>(std::max(bytes, size_t(8 * 1024))));
}
uint64_t modification(HalFile& file, const std::string& path) {
#ifdef SIMULATOR
  (void)file;
  const char* root = std::getenv("CROSSPOINT_SIM_SD");
  if (!root || !*root) root = std::getenv("CROSSPOINT_EMU_SD");
  const std::string native = std::string(root && *root ? root : "./fs_") + path;
  struct stat info{};
  if (::stat(native.c_str(), &info) != 0) return 0;
#ifdef __APPLE__
  return static_cast<uint64_t>(info.st_mtimespec.tv_sec) * 1000000000ULL + info.st_mtimespec.tv_nsec;
#else
  return static_cast<uint64_t>(info.st_mtim.tv_sec) * 1000000000ULL + info.st_mtim.tv_nsec;
#endif
#else
  (void)path;
  return file.modificationStamp();
#endif
}
bool directoryError(HalFile& file) {
#ifdef SIMULATOR
  (void)file;
  return errno != 0;
#else
  return file.readError();
#endif
}
// Only a validated generation is rotated into MAIN; a crash between renames
// leaves BACKUP recoverable. TEMP is never promoted during startup.
bool rotate(const char* main, const char* backup, const char* temp) {
  if (Storage.exists(backup) && !Storage.remove(backup)) return false;
  const bool hadMain = Storage.exists(main);
  if (hadMain && !Storage.rename(main, backup)) return false;
  if (Storage.rename(temp, main)) return true;
  if (hadMain) Storage.rename(backup, main);
  return false;
}
bool journalReadError(HalFile& file) {
#ifdef SIMULATOR
  (void)file;
  return errno != 0;
#else
  return file.readError();
#endif
}
void clearJournalReadError() {
#ifdef SIMULATOR
  errno = 0;
#endif
}
// State journal v1 reuses checked catalog records: added is the journal
// version and opened is Automatic/Pending/Reading/Finished. A torn last append
// leaves the previous state readable; repair publishes only the valid prefix.
bool stateRecord(const LibraryStore::Book& event) {
  return event.added == 1 && event.opened <= static_cast<uint32_t>(LibraryStore::ReadingState::Finished);
}
bool appendState(const std::string& path, LibraryStore::ReadingState state, bool clearOnlyPending = false) {
  if (state == LibraryStore::ReadingState::Automatic && !Storage.exists(STATES) && !Storage.exists(STATES_BACKUP))
    return true;
  if (path.empty() || path.front() != '/' || path.size() > MAX_PATH || path.find('\0') != std::string::npos ||
      state > LibraryStore::ReadingState::Finished || !heapAvailable() ||
      !Storage.ensureDirectoryExists("/.crosspoint"))
    return false;
  const char* source = Storage.exists(STATES) ? STATES : STATES_BACKUP;
  auto file = Storage.open(source);
  if (!file && Storage.exists(source)) return false;
  LibraryStore::Book event;
  size_t valid = 0;
  auto last = LibraryStore::ReadingState::Automatic;
  bool resourceFailure = false;
  clearJournalReadError();
  while (file && file.position() < file.size()) {
    if (!readRecord(file, event, &resourceFailure)) break;
    if (!stateRecord(event)) return false;
    valid = file.position();
    if (event.path == path) last = static_cast<LibraryStore::ReadingState>(event.opened);
  }
  if (resourceFailure || (file && journalReadError(file))) return false;
  if (last == state || (clearOnlyPending && last != LibraryStore::ReadingState::Pending)) return true;
  if (file && (valid != file.size() || strcmp(source, STATES))) {
    auto clean = Storage.open(STATES_TEMP, O_WRONLY | O_CREAT | O_TRUNC);
    if (!clean || !file.seek(0)) return false;
    uint8_t buffer[128];
    size_t remaining = valid;
    while (remaining) {
      const size_t n = std::min(remaining, sizeof(buffer));
      if (!readExact(file, buffer, n) || !writeExact(clean, buffer, n)) return false;
      remaining -= n;
    }
    clean.flush();
    if (!clean.close()) return false;
    file = HalFile();
    if (!Storage.exists(STATES)) {
      if (!Storage.rename(STATES_TEMP, STATES)) return false;
    } else if (!rotate(STATES, STATES_BACKUP, STATES_TEMP))
      return false;
  }
  file = HalFile();
  event = LibraryStore::Book{};
  event.path = path;
  event.added = 1;
  event.opened = static_cast<uint32_t>(state);
  auto out = Storage.open(STATES, O_WRONLY | O_CREAT | O_APPEND);
  if (!out || !writeRecord(out, event)) return false;
  out.flush();
  return out.close();
}
}  // namespace

LibraryStore::~LibraryStore() { release(); }
void LibraryStore::closeDirectories() {
  // Unwind iteratively: a deeply nested SD tree must not recursively destroy
  // all parent handles on the small firmware task stack when cancelled.
  while (directory) {
    auto parent = std::move(directory->parent);
    directory = std::move(parent);
  }
}
void LibraryStore::release() {
  if (state == ScanState::Scanning || state == ScanState::Publishing) cancel();
  catalog = HalFile();
  output = HalFile();
  closeDirectories();
  nameBuffer.reset();
  sourceCheck = HalFile();
  pendingBook.reset();
  records.reset();
  used = capacity = batch = sequence = 0;
  loaded = false;
}
bool LibraryStore::appendIndex(const Index& entry) {
  if (used == capacity) {
    const uint32_t next = capacity ? std::min(capacity * 2, uint32_t(32767)) : 128;
    if (next <= capacity || next > 32767 || !heapAvailable(next * sizeof(Index))) return false;
    auto larger = std::unique_ptr<Index[]>(new (std::nothrow) Index[next]);
    if (!larger) return false;
    if (used) memcpy(larger.get(), records.get(), used * sizeof(Index));
    records = std::move(larger);
    capacity = next;
  }
  records[used++] = entry;
  return true;
}
bool LibraryStore::loadFile(const char* path) {
  catalog = Storage.open(path);
  used = 0;
  uint32_t count = 0;
  if (!header(catalog, count, batch, sequence)) {
    catalog = HalFile();
    return false;
  }
  Book book;
  for (uint32_t i = 0; i < count; ++i) {
    const auto offset = static_cast<uint32_t>(catalog.position());
    if (!heapAvailable() || !readRecord(catalog, book) ||
        !appendIndex({offset, pathHash(book.path), book.added, book.opened, false})) {
      catalog = HalFile();
      used = 0;
      return false;
    }
  }
  if (catalog.position() != catalog.size()) {
    catalog = HalFile();
    used = 0;
    return false;
  }
  activePath = path;
  loaded = true;
  return true;
}
bool LibraryStore::load(Order order) {
  release();
  ordering = order;
  readFailed = false;
  if (!loadFile(MAIN) && !loadFile(BACKUP)) {
    release();
    return false;
  }
  if (!applyHistory() || !markDuplicates() || !setOrder(order)) {
    release();
    return false;
  }
  return true;
}
bool LibraryStore::readAt(uint32_t offset, Book& book) {
  if (!catalog || !catalog.seek(offset) || !readRecord(catalog, book)) {
    readFailed = true;
    return false;
  }
  return true;
}
bool LibraryStore::readBook(uint32_t index, Book& book) {
  if (index >= used || !readAt(records[index].offset, book)) return false;
  book.opened = records[index].opened;
  book.duplicate = records[index].duplicate;
  book.readingState = records[index].readingState;
  book.manualState = records[index].manualState != ReadingState::Automatic;
  return true;
}
bool LibraryStore::matches(const Index& entry) const {
  return filtering == Filter::All || (filtering == Filter::Reading && entry.readingState == ReadingState::Reading) ||
         (filtering == Filter::Pending && entry.readingState == ReadingState::Pending) ||
         (filtering == Filter::Finished && entry.readingState == ReadingState::Finished);
}
uint32_t LibraryStore::visibleCount() const {
  if (filtering == Filter::All) return used;
  uint32_t count = 0;
  for (uint32_t i = 0; i < used; ++i)
    if (matches(records[i])) ++count;
  return count;
}
bool LibraryStore::readVisibleBook(uint32_t index, Book& book) {
  if (filtering == Filter::All) return readBook(index, book);
  for (uint32_t i = 0; i < used; ++i) {
    if (matches(records[i])) {
      if (!index) return readBook(i, book);
      --index;
    }
  }
  return false;
}
int LibraryStore::findVisiblePath(const std::string& path) {
  const int physical = findPath(path);
  if (physical < 0 || !matches(records[physical])) return -1;
  int visible = 0;
  for (int i = 0; i < physical; ++i)
    if (matches(records[i])) ++visible;
  return visible;
}
bool LibraryStore::refreshReadingStates(StateResolver resolver, void* context) {
  readFailed = false;
  Book book;
  for (uint32_t i = 0; i < used; ++i) {
    if (!readBook(i, book)) return false;
    records[i].automaticState = resolver      ? resolver(book, context)
                                : book.opened ? ReadingState::Reading
                                              : ReadingState::Pending;
    if (records[i].automaticState < ReadingState::Pending || records[i].automaticState > ReadingState::Finished)
      return false;
    records[i].manualState = ReadingState::Automatic;
  }
  if (!applyReadingStates()) return false;
  for (uint32_t i = 0; i < used; ++i)
    records[i].readingState =
        records[i].manualState != ReadingState::Automatic ? records[i].manualState : records[i].automaticState;
  return true;
}
bool LibraryStore::applyReadingStates() {
  const char* path = Storage.exists(STATES) ? STATES : STATES_BACKUP;
  auto file = Storage.open(path);
  if (!file) return !Storage.exists(path);
  Book event;
  bool resourceFailure = false;
  clearJournalReadError();
  // Resolve the latest override first; Automatic restores the resolver's base.
  while (file.position() < file.size()) {
    if (!readRecord(file, event, &resourceFailure)) break;
    if (!stateRecord(event)) return false;
    const int i = findPath(event.path);
    if (readFailed) return false;
    if (i >= 0) records[i].manualState = static_cast<ReadingState>(event.opened);
  }
  if (resourceFailure || journalReadError(file)) return false;
  return true;
}
bool LibraryStore::setReadingState(const std::string& path, ReadingState state) {
  if (state < ReadingState::Pending || state > ReadingState::Finished) return false;
  const int i = findPath(path);
  if (i < 0 || !appendState(path, state)) return false;
  records[i].manualState = records[i].readingState = state;
  return true;
}
bool LibraryStore::recordFinished(const std::string& path) {
  if (!Storage.exists(STATES) && !Storage.exists(STATES_BACKUP)) return true;
  const bool ok = appendState(path, ReadingState::Automatic);
  if (!ok) LOG_ERR("LIB", "Could not clear finished-book state override");
  return ok;
}
int LibraryStore::findPath(const std::string& path) {
  const auto hash = pathHash(path);
  Book book;
  for (uint32_t i = 0; i < used; ++i) {
    if (records[i].hash == hash) {
      if (!readBook(i, book)) return -1;
      if (book.path == path) return static_cast<int>(i);
    }
  }
  return -1;
}
bool LibraryStore::setOrder(Order order) {
  if (!loaded) return false;
  ordering = order;
  if (used < 2) return true;
  // An explicit merge stops immediately on SD read errors. std::sort cannot
  // safely change its comparator's ordering after an I/O failure.
  if (!heapAvailable(capacity * sizeof(Index))) return false;
  auto scratch = std::unique_ptr<Index[]>(new (std::nothrow) Index[capacity]);
  if (!scratch) return false;
  Book left, right;
  auto less = [&](const Index& a, const Index& b) {
    if (order == Order::LastRead && a.opened != b.opened) return a.opened > b.opened;
    if (order == Order::Added && a.added != b.added) return a.added > b.added;
    if (!readAt(a.offset, left) || !readAt(b.offset, right)) return false;
    if (order == Order::Author && left.author != right.author) {
      if (left.author.empty() != right.author.empty()) return !left.author.empty();
      if (FsHelpers::naturalLess(left.author, right.author)) return true;
      if (FsHelpers::naturalLess(right.author, left.author)) return false;
    }
    if (FsHelpers::naturalLess(left.title, right.title)) return true;
    if (FsHelpers::naturalLess(right.title, left.title)) return false;
    if (FsHelpers::naturalLess(left.path, right.path)) return true;
    if (FsHelpers::naturalLess(right.path, left.path)) return false;
    return left.path < right.path;
  };
  for (uint32_t width = 1; width < used; width *= 2) {
    for (uint32_t first = 0; first < used; first += width * 2) {
      const uint32_t middle = std::min(first + width, used), end = std::min(first + width * 2, used);
      uint32_t a = first, b = middle;
      for (uint32_t out = first; out < end; ++out) {
        bool takeLeft = b == end;
        if (a < middle && b < end) takeLeft = !less(records[b], records[a]);
        if (readFailed) return false;
        scratch[out] = takeLeft && a < middle ? records[a++] : records[b++];
      }
    }
    records.swap(scratch);
  }
  return true;
}
bool LibraryStore::markDuplicates() {
  // Metadata hashes find candidates; exact comparison prevents hash collisions
  // from displaying a spurious duplicate marker. No strings are kept resident.
  if (used < 2) return true;
  if (!heapAvailable(used * sizeof(uint32_t))) return false;
  auto hashes = std::unique_ptr<uint32_t[]>(new (std::nothrow) uint32_t[used]);
  if (!hashes) return false;
  Book book, other;
  for (uint32_t i = 0; i < used; ++i) {
    if (!readBook(i, book)) return false;
    hashes[i] = hashBytes(pathHash(book.title), book.author.data(), book.author.size());
  }
  for (uint32_t i = 0; i < used; ++i) {
    for (uint32_t j = 0; j < i; ++j) {
      if (hashes[i] != hashes[j]) continue;
      if (!readBook(i, book) || !readBook(j, other)) return false;
      if (book.title == other.title && book.author == other.author) records[i].duplicate = records[j].duplicate = true;
    }
  }
  return true;
}
bool LibraryStore::applyHistory() {
  const char* path = Storage.exists(HISTORY) ? HISTORY : "/.crosspoint/library-open.bin.bak";
  auto file = Storage.open(path);
  if (!file) return !Storage.exists(path);
  Book event;
  bool resourceFailure = false;
  clearJournalReadError();
  while (file.position() < file.size()) {
    if (!readRecord(file, event, &resourceFailure)) break;  // incomplete final append: keep the valid prefix
    sequence = std::max(sequence, event.opened);
    const int i = findPath(event.path);
    if (readFailed) return false;
    if (i >= 0) records[i].opened = std::max(records[i].opened, event.opened);
  }
  return !resourceFailure && !journalReadError(file);
}
bool LibraryStore::recordOpened(const std::string& path) {
  if (path.empty() || path.size() > MAX_PATH || !heapAvailable() || !Storage.ensureDirectoryExists("/.crosspoint"))
    return false;
  // This shared hook is called by RecentBooksStore after the reader has opened
  // the book successfully, including opens outside Library.
  uint32_t count = 0, batch = 0, sequence = 0;
  auto main = Storage.open(MAIN);
  if (!header(main, count, batch, sequence)) {
    main = Storage.open(BACKUP);
    header(main, count, batch, sequence);
  }
  main = HalFile();
  const char* source = Storage.exists(HISTORY) ? HISTORY : "/.crosspoint/library-open.bin.bak";
  auto file = Storage.open(source);
  if (!file && Storage.exists(source)) return false;
  Book event;
  size_t valid = 0;
  bool resourceFailure = false;
  clearJournalReadError();
  while (file && file.position() < file.size()) {
    if (!readRecord(file, event, &resourceFailure)) break;
    valid = file.position();
    sequence = std::max(sequence, event.opened);
  }
  if (resourceFailure || (file && journalReadError(file))) return false;
  const bool repair = file && (valid != file.size() || strcmp(source, HISTORY));
  if (repair) {
    auto clean = Storage.open("/.crosspoint/library-open.bin.tmp", O_WRONLY | O_CREAT | O_TRUNC);
    if (!clean || !file.seek(0)) return false;
    uint8_t buffer[128];
    size_t remaining = valid;
    while (remaining) {
      const size_t n = std::min(remaining, sizeof(buffer));
      if (!readExact(file, buffer, n) || !writeExact(clean, buffer, n)) return false;
      remaining -= n;
    }
    clean.flush();
    clean = HalFile();
    file = HalFile();
    // A recovered backup must not be removed before MAIN is restored.
    if (!Storage.exists(HISTORY)) {
      if (!Storage.rename("/.crosspoint/library-open.bin.tmp", HISTORY)) return false;
    } else if (!rotate(HISTORY, "/.crosspoint/library-open.bin.bak", "/.crosspoint/library-open.bin.tmp"))
      return false;
  }
  file = HalFile();
  if (sequence == std::numeric_limits<uint32_t>::max()) return false;
  event = Book{};
  event.path = path;
  event.opened = sequence + 1;
  auto out = Storage.open(HISTORY, O_WRONLY | O_CREAT | O_APPEND);
  if (!out || !writeRecord(out, event)) return false;
  out.flush();
  const bool saved = out.close();
  if (saved && !appendState(path, ReadingState::Automatic, true))
    LOG_ERR("LIB", "Could not clear pending-book override");
  return saved;
}

bool LibraryStore::beginScan() {
  if (state == ScanState::Scanning || state == ScanState::Publishing) return false;
  if (!heapAvailable(4096) || batch == std::numeric_limits<uint32_t>::max() ||
      !Storage.ensureDirectoryExists("/.crosspoint")) {
    fail();
    return false;
  }
  directory.reset(new (std::nothrow) Directory());
  nameBuffer.reset(new (std::nothrow) char[NAME_SIZE]);
  if (!directory || !nameBuffer) {
    fail();
    return false;
  }
  directory->file = Storage.open("/");
  directory->path = "/";
  output = Storage.open(TEMP, O_RDWR | O_CREAT | O_TRUNC);
  if (!directory->file || !directory->file.isDirectory() || !output || !writeHeader(output, 0, batch + 1, sequence)) {
    fail();
    return false;
  }
  foundBooks = 0;
  readFailed = false;
  state = ScanState::Scanning;
  return true;
}
LibraryStore::ScanState LibraryStore::fail() {
  state = ScanState::Failed;
  output = HalFile();
  closeDirectories();
  nameBuffer.reset();
  sourceCheck = HalFile();
  pendingBook.reset();
  if (Storage.exists(TEMP)) Storage.remove(TEMP);
  return state;
}
void LibraryStore::cancel() {
  state = ScanState::Cancelled;
  output = HalFile();
  closeDirectories();
  nameBuffer.reset();
  sourceCheck = HalFile();
  pendingBook.reset();
  if (Storage.exists(TEMP)) Storage.remove(TEMP);
}
LibraryStore::ScanState LibraryStore::step() {
  if (state == ScanState::Publishing) {
    if (!publish()) return fail();
    state = ScanState::Complete;
    return state;
  }
  if (state != ScanState::Scanning) return state;
  if (pendingBook) {
    // Distinguish malformed metadata from an SD read failure. Verify a broken
    // source in small batches, keeping Back/cancellation responsive.
    for (int i = 0; i < 8 && sourceCheck.position() < pendingBook->size; ++i) {
      const size_t n = static_cast<size_t>(std::min(uint64_t(NAME_SIZE), pendingBook->size - sourceCheck.position()));
      if (!readExact(sourceCheck, nameBuffer.get(), n)) return fail();
    }
    if (sourceCheck.position() < pendingBook->size) return state;
    if (sourceCheck.fileSize64() != pendingBook->size ||
        modification(sourceCheck, pendingBook->path) != pendingBook->modified)
      return fail();
    if (!writeRecord(output, *pendingBook)) return fail();
    pendingBook.reset();
    sourceCheck = HalFile();
    if (++foundBooks > 32767) return fail();
    return state;
  }
  if (!directory) {
    state = ScanState::Publishing;
    return state;
  }
  if (!heapAvailable()) return fail();
#ifdef SIMULATOR
  errno = 0;
#endif
  auto entry = directory->file.openNextFile();
  if (!entry) {
    if (directoryError(directory->file)) return fail();
    auto parent = std::move(directory->parent);
    directory = std::move(parent);
    return state;
  }
  const auto length = entry.getName(nameBuffer.get(), NAME_SIZE);
  if (!length || length >= NAME_SIZE - 1) return fail();
  std::string name(nameBuffer.get());
  if (excluded(name)) return state;
  const std::string path = directory->path == "/" ? "/" + name : directory->path + "/" + name;
  if (path.size() > MAX_PATH) return fail();
  if (entry.isDirectory()) {
    auto child = std::unique_ptr<Directory>(new (std::nothrow) Directory());
    if (!child) return fail();
    child->file = std::move(entry);
    child->path = path;
    child->parent = std::move(directory);
    directory = std::move(child);
    return state;
  }
  if (!supported(name)) return state;
  Book book;
  book.path = path;
  book.size = entry.fileSize64();
  book.modified = modification(entry, path);
  entry = HalFile();
  const int old = findPath(path);
  if (readFailed) return fail();
  Book previous;
  if (old >= 0 && !readBook(old, previous)) return fail();
  book.added = old >= 0 ? previous.added : batch + 1;
  book.opened = old >= 0 ? previous.opened : 0;
  if (old < 0 && !loaded) {
    const auto& recent = RECENT_BOOKS.getBooks();
    for (size_t i = 0; i < recent.size(); ++i) {
      if (recent[i].path == path) book.opened = static_cast<uint32_t>(recent.size() - i);
    }
    sequence = std::max(sequence, book.opened);
  }
  if (old >= 0 && book.modified && book.size == previous.size && book.modified == previous.modified) {
    book.title = std::move(previous.title);
    book.author = std::move(previous.author);
  } else {
    if (!MemoryBudget::hasHeap(MemoryBudget::snapshot(), 64 * 1024, 32 * 1024)) return fail();
    const bool metadataValid = readLibraryMetadata(path, book.title, book.author);
    // Unreadable source aborts the transaction. Malformed metadata alone uses
    // the filename fallback, so a broken EPUB cannot hide other books.
    auto probe = Storage.open(path.c_str());
    uint8_t byte;
    if (!probe || probe.fileSize64() != book.size || modification(probe, path) != book.modified ||
        (book.size && probe.read(&byte, 1) != 1))
      return fail();
    if (!metadataValid) {
      book.title = fallbackTitle(path);
      book.author.clear();
      pendingBook.reset(new (std::nothrow) Book(std::move(book)));
      if (!pendingBook || !probe.seek(0)) return fail();
      sourceCheck = std::move(probe);
      return state;
    }
  }
  if (book.title.empty()) book.title = fallbackTitle(path);
  if (!writeRecord(output, book)) return fail();
  if (++foundBooks > 32767) return fail();
  return state;
}
bool LibraryStore::publish() {
  if (!writeHeader(output, foundBooks, batch + 1, sequence)) return false;
  output.flush();
  output = HalFile();
  closeDirectories();
  nameBuffer.reset();
  sourceCheck = HalFile();
  pendingBook.reset();
  // Validate every checksum without allocating a second catalog index.
  auto verify = Storage.open(TEMP);
  uint32_t count = 0, newBatch = 0, newSequence = 0;
  if (!header(verify, count, newBatch, newSequence) || count != foundBooks) return false;
  Book book;
  for (uint32_t i = 0; i < count; ++i)
    if (!readRecord(verify, book)) return false;
  if (verify.position() != verify.size()) return false;
  verify = HalFile();
  const bool recovering = activePath == BACKUP;
  catalog = HalFile();
  // Preserve the known-good backup when MAIN was corrupt at startup.
  if (recovering) {
    if (Storage.exists(MAIN) && !Storage.remove(MAIN)) return false;
    if (!Storage.rename(TEMP, MAIN)) return false;
  } else if (!rotate(MAIN, BACKUP, TEMP))
    return false;
  const auto order = ordering;
  // release() must not cancel the already-published transaction.
  state = ScanState::Complete;
  return load(order);
}
