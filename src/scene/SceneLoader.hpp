#pragma once

#include <string>
#include <unordered_map>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "SceneData.hpp"

class SceneLoader
{
public:
  SceneLoader() = default;
  ~SceneLoader() = default;

  /**
   * @brief Parses a JSON scene file and populates the intermediate SceneData
   * struct.
   * @param filepath Path to the .json file.
   * @param outScene Reference to the SceneData struct to fill.
   * @return true if successful, false otherwise.
   */
  [[nodiscard]] bool load(const std::string& filepath, SceneData& outScene);

private:
  // Convenience typedef for name-to-index lookups
  using IDMap = std::unordered_map<std::string, int>;
  using json = nlohmann::json;

  // --- Parsing Helpers ---
  void parseAssets(const json& j, SceneData& scene);
  void parseMaterials(const json& j, SceneData& scene);
  void parseInstances(const json& j, SceneData& scene);
  void parseSceneInfo(const json& j, SceneData& scene);

  // --- Utility / Math Converters ---
  glm::vec3 parseVec3(const json& j,
                      const glm::vec3& defaultValue = glm::vec3(0.0f));
  glm::vec4 parseVec4(const json& j,
                      const glm::vec4& defaultValue = glm::vec4(1.0f));
  glm::vec2 parseVec2(const json& j,
                      const glm::vec2& defaultValue = glm::vec2(0.0f));

  // --- Enum Converters ---
  MaterialType parseHitGroup(const std::string& type);
  shaderio::LightType parseLightType(const std::string& type);
};
