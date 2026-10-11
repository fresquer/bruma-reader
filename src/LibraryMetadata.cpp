#include "LibraryMetadata.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <MemoryBudget.h>
#include <Utf8.h>
#include <XmlParserUtils.h>
#include <Xtc.h>

#include <cstring>

#include "Epub/parsers/ContainerParser.h"
#include "expat.h"

namespace {
class MetadataParser final : public Print {
 public:
  XML_Parser parser = nullptr;
  std::string title, author;
  size_t remaining;
  unsigned depth = 0, metadataDepth = 0, captureDepth = 0;
  std::string* capture = nullptr;
  explicit MetadataParser(size_t size) : remaining(size) {}
  ~MetadataParser() override { destroyXmlParser(parser); }
  bool setup() {
    parser = XML_ParserCreateNS(nullptr, '|');
    if (!parser) return false;
    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, start, end);
    XML_SetCharacterDataHandler(parser, text);
    return true;
  }
  static const char* local(const char* name) {
    const auto* p = strrchr(name, '|');
    return p ? p + 1 : xmlLocalName(name);
  }
  static void start(void* ctx, const char* name, const char**) {
    auto& self = *static_cast<MetadataParser*>(ctx);
    ++self.depth;
    if (!strcmp(local(name), "metadata")) self.metadataDepth = self.depth;
    if (!self.metadataDepth || self.capture) return;
    if (!strcmp(local(name), "title") && self.title.empty()) self.capture = &self.title;
    if (!strcmp(local(name), "creator") && self.author.empty()) self.capture = &self.author;
    if (self.capture) self.captureDepth = self.depth;
  }
  static void end(void* ctx, const char*) {
    auto& self = *static_cast<MetadataParser*>(ctx);
    if (self.depth == self.captureDepth) {
      self.capture = nullptr;
      self.captureDepth = 0;
    }
    if (self.depth == self.metadataDepth) self.metadataDepth = 0;
    --self.depth;
  }
  static void text(void* ctx, const char* data, int length) {
    auto& self = *static_cast<MetadataParser*>(ctx);
    if (!self.capture) return;
    const size_t limit = self.capture == &self.title ? 512 : 256;
    const size_t n = std::min(static_cast<size_t>(length), limit - self.capture->size());
    self.capture->append(data, n);
  }
  size_t write(uint8_t data) override { return write(&data, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    if (!parser || size > remaining) return 0;
    remaining -= size;
    return XML_Parse(parser, reinterpret_cast<const char*>(data), static_cast<int>(size), remaining == 0) ==
                   XML_STATUS_ERROR
               ? 0
               : size;
  }
};
void clean(std::string& value) {
  for (char& c : value)
    if (c == '\r' || c == '\n' || c == '\t') c = ' ';
  const auto first = value.find_first_not_of(' ');
  if (first == std::string::npos) {
    value.clear();
    return;
  }
  value = value.substr(first, value.find_last_not_of(' ') - first + 1);
  // Drop only an incomplete final UTF-8 sequence, preserving valid accents.
  size_t lead = value.size() - 1;
  while (lead && (static_cast<unsigned char>(value[lead]) & 0xc0) == 0x80) --lead;
  const auto c = static_cast<unsigned char>(value[lead]);
  const size_t needed = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
  if (value.size() - lead < needed) value.resize(lead);
}
}  // namespace

bool readLibraryMetadata(const std::string& path, std::string& title, std::string& author) {
  title.clear();
  author.clear();
  if (!MemoryBudget::hasHeap(MemoryBudget::snapshot(), 64 * 1024, 32 * 1024)) return false;
  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    size_t size = 0;
    if (!epub.getItemSize("META-INF/container.xml", &size) || size > 8 * 1024) return false;
    std::string opf;
    {
      ContainerParser container(size);
      if (!container.setup() || !epub.readItemContentsToStream("META-INF/container.xml", container, 512)) return false;
      opf = std::move(container.fullPath);
    }
    if (opf.empty() || !epub.getItemSize(opf, &size) || size > 4 * 1024 * 1024) return false;
    MetadataParser metadata(size);
    if (!metadata.setup() || !epub.readItemContentsToStream(opf, metadata, 512)) return false;
    title = utf8ComposeNfc(metadata.title);
    author = utf8ComposeNfc(metadata.author);
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc xtc(path, "/.crosspoint");
    if (!xtc.load()) return false;
    title = xtc.getTitle().substr(0, 512);
    author = xtc.getAuthor().substr(0, 256);
  }
  clean(title);
  clean(author);
  return true;
}
