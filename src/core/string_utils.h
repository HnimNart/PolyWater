#pragma once

#include <cctype>
#include <string>

namespace common {

std::string capitalize(std::string str) {
  if (!str.empty()) {
    // Use unsigned char cast for safety with std::toupper
    str[0] =
        static_cast<char>(std::toupper(static_cast<unsigned char>(str[0])));
  }
  return str;
}

} // namespace common
