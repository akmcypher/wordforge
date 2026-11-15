#pragma once
#include <functional>
#include <optional>
#include "core/document/document.hpp"
namespace wf {
struct DocxImport {
    Document document;
    std::vector<std::string> warnings;
};
// Converts picture bytes in a format other than PNG (JPEG, GIF, BMP...) to PNG; returns
// nothing when it cannot. Core code stays free of image codecs; the desktop supplies one.
using PictureConverter = std::function<std::optional<std::string>(const std::string &)>;
DocxImport parseDocx(const std::string &, const PictureConverter & = {});
std::string serializeDocx(const Document &);
} // namespace wf
