#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace core {

inline std::string capitalize(std::string str) {
  if (!str.empty()) {
    // Use unsigned char cast for safety with std::toupper
    str[0] =
        static_cast<char>(std::toupper(static_cast<unsigned char>(str[0])));
  }
  return str;
}

inline std::string getExtension(const std::string &filename) {
  // 1. Extract Extension
  std::string ext = "";
  size_t dotPos = filename.find_last_of(".");
  if (dotPos != std::string::npos) {
    ext = filename.substr(dotPos); // Keep the dot (e.g., ".obj")
  }
  return ext;
}

inline void toLower(std::string &str) {
  std::transform(str.begin(), str.end(), str.begin(),
                 [](unsigned char c) { return std::tolower(c); });
}

// -----------------------------------------------------------------------
// Extracts the filename without extension and converts it to lowercase
// -----------------------------------------------------------------------
inline std::string getLowercasedStem(const std::string &filepath) {
  // 1. Extract the stem
  std::string stem = std::filesystem::path(filepath).stem().string();

  // 2. Convert to lowercase in place
  std::transform(stem.begin(), stem.end(), stem.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  return stem;
}

inline std::string getDirectory(const std::string& filepath) {
    return std::filesystem::path(filepath).parent_path().string();
}

/**
 * @brief Removes leading and trailing whitespace from a string.
 * Whitespace includes spaces ' ', tabs '\t', and newlines '\n' or '\r'.
 */
inline std::string trim(const std::string &str) {
  // 1. Find the first character that isn't whitespace
  const auto first = str.find_first_not_of(" \t\n\r");

  // 2. If no non-whitespace characters were found, return an empty string
  if (first == std::string::npos) {
    return "";
  }

  // 3. Find the last character that isn't whitespace
  const auto last = str.find_last_not_of(" \t\n\r");

  // 4. Calculate the length and return the substring
  return str.substr(first, (last - first + 1));
}

} // namespace core
