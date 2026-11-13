#pragma once
#include "core/document/document.hpp"
namespace wf {
struct DocxImport {
    Document document;
    std::vector<std::string> warnings;
};
DocxImport parseDocx(const std::string &);
std::string serializeDocx(const Document &);
} // namespace wf
