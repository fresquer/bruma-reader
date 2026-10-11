#pragma once
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

namespace LibraryFault {
inline std::string root, failRead, failWrite, failRename, failDirectory, missingTimestamp;
inline size_t readsUntilFailure = SIZE_MAX;
inline void reset() {
  failRead.clear();
  failWrite.clear();
  failRename.clear();
  failDirectory.clear();
  readsUntilFailure = SIZE_MAX;
}
}  // namespace LibraryFault
class HalStorage;
class HalFile {
  friend class HalStorage;
  int fd = -1;
  DIR* dir = nullptr;
  std::string path;
  bool error = false;
  void move(HalFile&& other) {
    fd = std::exchange(other.fd, -1);
    dir = std::exchange(other.dir, nullptr);
    path = std::move(other.path);
    error = other.error;
  }

 public:
  HalFile() = default;
  ~HalFile() { close(); }
  HalFile(HalFile&& other) { move(std::move(other)); }
  HalFile& operator=(HalFile&& other) {
    close();
    move(std::move(other));
    return *this;
  }
  HalFile(const HalFile&) = delete;
  bool close() {
    bool ok = true;
    if (fd >= 0) ok = ::close(fd) == 0;
    if (dir) ok = ::closedir(dir) == 0 && ok;
    fd = -1;
    dir = nullptr;
    return ok;
  }
  explicit operator bool() const { return fd >= 0 || dir; }
  bool isDirectory() const { return dir; }
  bool readError() const { return error; }
  uint64_t modificationStamp() const {
    if (!LibraryFault::missingTimestamp.empty() && path.ends_with(LibraryFault::missingTimestamp)) return 0;
    struct stat info{};
    if (::stat(path.c_str(), &info)) return 0;
#ifdef __APPLE__
    return uint64_t(info.st_mtimespec.tv_sec) * 1000000000 + info.st_mtimespec.tv_nsec;
#else
    return uint64_t(info.st_mtim.tv_sec) * 1000000000 + info.st_mtim.tv_nsec;
#endif
  }
  size_t getName(char* data, size_t length) const {
    const auto name = std::filesystem::path(path).filename().string();
    const auto n = std::min(length - 1, name.size());
    memcpy(data, name.data(), n);
    data[n] = 0;
    return n;
  }
  size_t size() const {
    struct stat s{};
    return ::fstat(fd, &s) ? 0 : s.st_size;
  }
  uint64_t fileSize64() const { return size(); }
  size_t position() const { return ::lseek(fd, 0, SEEK_CUR); }
  bool seek(size_t p) { return ::lseek(fd, p, SEEK_SET) >= 0; }
  int read(void* bytes, size_t count) {
    if (!LibraryFault::failRead.empty() && path.ends_with(LibraryFault::failRead)) {
      if (!LibraryFault::readsUntilFailure) {
        error = true;
        return -1;
      }
      --LibraryFault::readsUntilFailure;
    }
    const auto result = ::read(fd, bytes, count);
    if (result < 0) error = true;
    return static_cast<int>(result);
  }
  size_t write(const void* bytes, size_t count) {
    if (!LibraryFault::failWrite.empty() && path.ends_with(LibraryFault::failWrite)) return 0;
    const auto n = ::write(fd, bytes, count);
    return n < 0 ? 0 : n;
  }
  void flush() {
    if (fd >= 0) ::fsync(fd);
  }
  HalFile openNextFile();
};
class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }
  std::string resolve(const char* path) const { return LibraryFault::root + path; }
  HalFile open(const char* logical, int flags = O_RDONLY) const {
    HalFile file;
    file.path = resolve(logical);
    struct stat s{};
    if (::stat(file.path.c_str(), &s) == 0 && S_ISDIR(s.st_mode))
      file.dir = ::opendir(file.path.c_str());
    else
      file.fd = ::open(file.path.c_str(), flags, 0600);
    return file;
  }
  bool exists(const char* p) const { return std::filesystem::exists(resolve(p)); }
  bool remove(const char* p) const { return ::unlink(resolve(p).c_str()) == 0; }
  bool rename(const char* a, const char* b) const {
    if (!LibraryFault::failRename.empty() && std::string(b).ends_with(LibraryFault::failRename)) return false;
    return ::rename(resolve(a).c_str(), resolve(b).c_str()) == 0;
  }
  bool ensureDirectoryExists(const char* p) const {
    std::error_code error;
    std::filesystem::create_directories(resolve(p), error);
    return !error;
  }
};
#define Storage HalStorage::getInstance()
inline HalFile HalFile::openNextFile() {
  if (!LibraryFault::failDirectory.empty() && path.ends_with(LibraryFault::failDirectory)) {
    error = true;
    return {};
  }
  while (const auto* entry = ::readdir(dir)) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
    return Storage.open((path.substr(LibraryFault::root.size()) + "/" + entry->d_name).c_str());
  }
  return {};
}
