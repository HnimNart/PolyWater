#pragma once

#include "SceneData.hpp"
#include <string>

class SceneSaver {
public:
  /**
   * @brief Serializes the live CPU-side SceneData into a formatted JSON file.
   * * @param filepath The destination path (e.g.,
   * "assets/scenes/my_level.json")
   * @param scene    The extracted SceneData struct containing meshes,
   * materials, and instances
   * @return true    If the file was successfully written to disk
   * @return false   If there was an error opening the file for writing
   */
  static bool save(const std::string &filepath, const SceneData &scene);
};
