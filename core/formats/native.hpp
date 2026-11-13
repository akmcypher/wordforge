#pragma once
#include "core/document/document.hpp"
namespace wf {
// Versioned XML (WFG1/2/3) or bounded image ZIP (WFG4); no external entity resolution.
std::string serializeNative(const Document &);
Document parseNative(const std::string &);
} // namespace wf
