#pragma once
#include <map>
#include <string>
namespace wf::opc {
using Parts = std::map<std::string, std::string>;
Parts read(const std::string &);
std::string write(const Parts &);
} // namespace wf::opc
