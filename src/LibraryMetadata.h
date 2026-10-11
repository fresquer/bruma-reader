#pragma once
#include <string>
// Reads only source metadata, without producing reading caches or thumbnails.
// False means unavailable/malformed metadata; the caller verifies source reads
// before accepting a filename fallback.
bool readLibraryMetadata(const std::string& path, std::string& title, std::string& author);
