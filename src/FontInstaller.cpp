#include "FontInstaller.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <SdCardFont.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "CrossPointSettings.h"
#include "util/CheckedFileCopy.h"

FontInstaller::FontInstaller(SdCardFontRegistry& registry) : registry_(registry) {}

bool FontInstaller::isValidFamilyName(const char* name) {
  if (name == nullptr || name[0] == '\0') return false;

  // Reject path traversal
  if (strstr(name, "..") != nullptr) return false;
  if (strchr(name, '/') != nullptr) return false;
  if (strchr(name, '\\') != nullptr) return false;

  for (const char* p = name; *p != '\0'; ++p) {
    char c = *p;
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
      return false;
    }
  }
  return true;
}

bool FontInstaller::isValidCpfontFilename(const char* name) {
  if (name == nullptr || name[0] == '\0') return false;

  // Reject path separators / traversal up front. Anything that could escape
  // the family directory or refer to a different one is a hard reject.
  if (strstr(name, "..") != nullptr) return false;
  if (strchr(name, '/') != nullptr) return false;
  if (strchr(name, '\\') != nullptr) return false;

  // Must end with ".cpfont" exactly.
  static constexpr char kExt[] = ".cpfont";
  static constexpr size_t kExtLen = sizeof(kExt) - 1;
  size_t nameLen = strlen(name);
  if (nameLen <= kExtLen) return false;
  if (strcmp(name + nameLen - kExtLen, kExt) != 0) return false;

  // Basename (before .cpfont) must be alphanumeric + hyphen + underscore only.
  // No additional dots — keeps stray "Foo.cpfont.tmp"-style names out.
  size_t baseLen = nameLen - kExtLen;
  for (size_t i = 0; i < baseLen; ++i) {
    char c = name[i];
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
      return false;
    }
  }
  return true;
}

bool FontInstaller::ensureFamilyDir(const char* familyName) {
  // Reuse the family's existing root if installed; otherwise pick the
  // default-write root (hidden if no roots exist yet).
  char root[16];
  if (!SdCardFontRegistry::findFamilyRoot(familyName, root, sizeof(root))) {
    SdCardFontRegistry::defaultWriteRoot(root, sizeof(root));
  }

  if (!Storage.exists(root)) {
    if (!Storage.mkdir(root)) {
      LOG_ERR("FONT", "Failed to create fonts dir: %s", root);
      return false;
    }
  }

  char dirPath[160];
  snprintf(dirPath, sizeof(dirPath), "%s/%s", root, familyName);

  if (!Storage.exists(dirPath)) {
    if (!Storage.mkdir(dirPath)) {
      LOG_ERR("FONT", "Failed to create family dir: %s", dirPath);
      return false;
    }
  }
  return true;
}

bool FontInstaller::validateCpfontFile(const char* path) {
  HalFile file;
  if (!Storage.openFileForRead("FONT", path, file)) {
    LOG_ERR("FONT", "Cannot open for validation: %s", path);
    return false;
  }

  uint8_t magic[CPFONT_MAGIC_LEN];
  size_t bytesRead = file.read(magic, CPFONT_MAGIC_LEN);
  file.close();

  if (bytesRead < CPFONT_MAGIC_LEN) {
    LOG_ERR("FONT", "File too small: %s (%zu bytes)", path, bytesRead);
    return false;
  }

  if (memcmp(magic, "CPFONT\0\0", CPFONT_MAGIC_LEN) != 0) {
    LOG_ERR("FONT", "Bad magic in: %s", path);
    return false;
  }

  return true;
}

void FontInstaller::buildFontPath(const char* family, const char* filename, char* outBuf, size_t outBufSize) {
  // Use the same root selection as ensureFamilyDir: existing install dir wins,
  // otherwise the default-write root.
  char root[16];
  if (!SdCardFontRegistry::findFamilyRoot(family, root, sizeof(root))) {
    SdCardFontRegistry::defaultWriteRoot(root, sizeof(root));
  }
  snprintf(outBuf, outBufSize, "%s/%s/%s", root, family, filename);
}

FontInstaller::Error FontInstaller::deleteFamily(const char* familyName) {
  if (!isValidFamilyName(familyName)) {
    return Error::INVALID_FAMILY_NAME;
  }

  // A family may exist in either root (or, edge case, both). Remove from both.
  char hiddenRoot[16];
  char visibleRoot[16];
  const bool hasHidden =
      FsHelpers::resolveRootDirectoryIgnoreCase(SdCardFontRegistry::FONTS_DIR_HIDDEN, hiddenRoot, sizeof(hiddenRoot));
  const bool hasVisible = FsHelpers::resolveRootDirectoryIgnoreCase(SdCardFontRegistry::FONTS_DIR_VISIBLE, visibleRoot,
                                                                    sizeof(visibleRoot));
  const char* roots[] = {hasHidden ? hiddenRoot : SdCardFontRegistry::FONTS_DIR_HIDDEN,
                         hasVisible ? visibleRoot : SdCardFontRegistry::FONTS_DIR_VISIBLE};
  bool removedAny = false;
  bool sawAny = false;
  for (const char* root : roots) {
    char dirPath[160];
    snprintf(dirPath, sizeof(dirPath), "%s/%s", root, familyName);
    if (!Storage.exists(dirPath)) continue;
    sawAny = true;
    if (!Storage.removeDir(dirPath)) {
      LOG_ERR("FONT", "Failed to remove family dir: %s", dirPath);
      return Error::SD_WRITE_ERROR;
    }
    removedAny = true;
  }

  if (!sawAny) {
    LOG_DBG("FONT", "Family not found in any fonts root: %s", familyName);
    return Error::OK;  // Already gone
  }
  (void)removedAny;

  // If this was the active font, clear the setting
  if (strcmp(SETTINGS.sdFontFamilyName, familyName) == 0) {
    SETTINGS.clearSdFontFamily();
    LOG_DBG("FONT", "Cleared active SD font (deleted family: %s)", familyName);
  }

  return Error::OK;
}

void FontInstaller::refreshRegistry() { registry_.discover(); }

bool FontInstaller::isFamilyInstalled(const char* familyName) const {
  return registry_.findFamily(familyName) != nullptr;
}

FontInstaller::Error FontInstaller::installFromSd(const std::string& sourcePath, const bool replaceExisting) {
  const size_t slash = sourcePath.find_last_of('/');
  const std::string filename = sourcePath.substr(slash == std::string::npos ? 0 : slash + 1);
  if (!isValidCpfontFilename(filename.c_str()) || filename.size() > 127) return Error::INVALID_FILE;
  const size_t underscore = filename.find_last_of('_');
  const size_t extension = filename.size() - 7;
  if (underscore == std::string::npos || underscore == 0 || underscore + 1 >= extension) return Error::INVALID_FILE;
  unsigned size = 0;
  for (size_t i = underscore + 1; i < extension; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(filename[i]))) return Error::INVALID_FILE;
    size = size * 10 + filename[i] - '0';
    if (size > 255) return Error::INVALID_FILE;
  }
  if (size == 0) return Error::INVALID_FILE;
  const std::string family = filename.substr(0, underscore);
  if (family.size() >= sizeof(SETTINGS.sdFontFamilyName) || !isValidFamilyName(family.c_str()))
    return Error::INVALID_FAMILY_NAME;
  {
    auto font = makeUniqueNoThrow<SdCardFont>();
    if (!font || !font->load(sourcePath.c_str())) return Error::INVALID_FILE;
  }
  refreshRegistry();
  if (!isFamilyInstalled(family.c_str()) && registry_.getFamilyCount() >= SdCardFontRegistry::MAX_SD_FAMILIES)
    return Error::MAX_FAMILIES_REACHED;
  char destination[256];
  const std::string installedFilename = family + "_" + std::to_string(size) + ".cpfont";
  buildFontPath(family.c_str(), installedFilename.c_str(), destination, sizeof(destination));
  if (sourcePath == destination) return Error::OK;
  const bool existing = Storage.exists(destination);
  if (existing && !replaceExisting) return Error::FILE_EXISTS;
  if (!ensureFamilyDir(family.c_str())) return Error::SD_WRITE_ERROR;
  const std::string temporary = std::string(destination) + ".importing";
  const std::string backup = std::string(destination) + ".backup";
  // Never discard a previous interrupted replacement's recoverable backup.
  if (Storage.exists(backup.c_str())) return Error::SD_WRITE_ERROR;
  auto buffer = makeUniqueNoThrow<uint8_t[]>(4096);
  if (!buffer) return Error::SD_WRITE_ERROR;
  HalFile source, target;
  if (!Storage.openFileForRead("FONT", sourcePath.c_str(), source) ||
      !Storage.openFileForWrite("FONT", temporary.c_str(), target))
    return Error::SD_WRITE_ERROR;
  const size_t expected = source.size();
  const bool copied = checkedFileCopy(source, target, expected, buffer.get(), 4096, [] { yield(); });
  if (!copied) {
    Storage.remove(temporary.c_str());
    return Error::SD_WRITE_ERROR;
  }
  {
    auto font = makeUniqueNoThrow<SdCardFont>();
    if (!font || !font->load(temporary.c_str())) {
      Storage.remove(temporary.c_str());
      return Error::INVALID_FILE;
    }
  }
  if (existing && !Storage.rename(destination, backup.c_str())) {
    Storage.remove(temporary.c_str());
    return Error::SD_WRITE_ERROR;
  }
  if (!Storage.rename(temporary.c_str(), destination)) {
    if (existing) Storage.rename(backup.c_str(), destination);
    Storage.remove(temporary.c_str());
    return Error::SD_WRITE_ERROR;
  }
  if (existing) Storage.remove(backup.c_str());
  refreshRegistry();
  LOG_DBG("FONT", "Installed SD font: %s", destination);
  return Error::OK;
}
