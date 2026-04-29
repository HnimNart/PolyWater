#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace core
{

// ========================================================================
// String Manipulation Utilities
// ========================================================================

/**
 * @brief Capitalizes the first character of the given string.
 * @param str The input string.
 * @return A new string with the first character converted to uppercase.
 */
/**********************************************************/
inline std::string capitalize(std::string str)
/**********************************************************/
{
  if (!str.empty())
  {
    // Use unsigned char cast for safety with std::toupper
    str[0] =
        static_cast<char>(std::toupper(static_cast<unsigned char>(str[0])));
  }
  return str;
}

/**
 * @brief Converts all characters in the given string to lowercase in-place.
 * @param str The string to modify.
 */
/**********************************************************/
inline void toLower(std::string& str)
/**********************************************************/
{
  std::transform(str.begin(), str.end(), str.begin(),
                 [](unsigned char c) { return std::tolower(c); });
}

/**
 * @brief Removes leading and trailing whitespace from a string.
 * @param str The input string.
 * @return A new string with whitespace (' ', '\t', '\n', '\r') removed from
 * both ends.
 */
/**********************************************************/
inline std::string trim(const std::string& str)
/**********************************************************/
{
  const auto first = str.find_first_not_of(" \t\n\r");
  if (first == std::string::npos)
  {
    return "";
  }
  const auto last = str.find_last_not_of(" \t\n\r");
  return str.substr(first, (last - first + 1));
}

// ========================================================================
// File & Path Utilities
// ========================================================================

/**
 * @brief Extracts the directory path from a full filepath.
 * @param filepath The full path to a file.
 * @return The parent directory path (e.g., "assets/models").
 */
/**********************************************************/
inline std::string getDirectory(const std::string& filepath)
/**********************************************************/
{
  return std::filesystem::path(filepath).parent_path().string();
}

/**
 * @brief Extracts the filename (including extension) from a path.
 * @param path The full path.
 * @return The filename with its extension (e.g., "helmet.gltf").
 */
/**********************************************************/
inline std::string getFilename(const std::string& path)
/**********************************************************/
{
  return std::filesystem::path(path).filename().string();
}

/**
 * @brief Extracts the file extension, including the dot.
 * @param filename The filename or path.
 * @return The extension string (e.g., ".obj"), or an empty string if none
 * exists.
 */
/**********************************************************/
inline std::string getExtension(const std::string& filename)
/**********************************************************/
{
  std::string ext = "";
  size_t dotPos = filename.find_last_of(".");
  if (dotPos != std::string::npos)
  {
    ext = filename.substr(dotPos);
  }
  return ext;
}

/**
 * @brief Extracts the filename without its extension and converts it to
 * lowercase.
 * @param filepath The full path to the file.
 * @return The lowercased stem of the filename (e.g., "Helmet.gltf" ->
 * "helmet").
 */
/**********************************************************/
inline std::string getLowercasedStem(const std::string& filepath)
/**********************************************************/
{
  std::string stem = std::filesystem::path(filepath).stem().string();
  std::transform(stem.begin(), stem.end(), stem.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return stem;
}

/**********************************************************/
inline std::string getFileName(const std::string& path)
/**********************************************************/
{
  return std::filesystem::path(path).filename().string();
}

}  // namespace core
