#pragma once

#include <string>

namespace core {

std::string decodeText(const std::string &bytes,bool truncated=false);

// Returns true if the byte range is valid UTF-8.
bool isValidUtf8(const char *data, size_t len);

// Converts a GBK/GB2312/ANSI encoded string to UTF-8 using the embedded
// GBK->Unicode table. Bytes that cannot be mapped are replaced with '?'.
std::string gbkToUtf8(const char *data, size_t len);

// Ensures the given text is UTF-8: if it is already valid UTF-8 it is returned
// unchanged; otherwise it is treated as GBK and converted to UTF-8. This lets
// the text app transparently handle both legacy ANSI files and modern UTF-8
// files, as well as GBK-encoded file names coming from a Windows-formatted SD
// card.
std::string ensureUtf8(const std::string &text);

inline std::string ensureUtf8(const char *text)
{
    return ensureUtf8(std::string(text));
}

} // namespace core