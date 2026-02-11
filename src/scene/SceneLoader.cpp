#include "SceneLoader.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <core/logger.hpp>
#include <unordered_map>

#include "SceneData.hpp"

// Helper macro for cleaner JSON lookups with default values
#define JSON_VAL(jsonObj, key, defaultVal)                                     \
  (jsonObj.contains(key) ? jsonObj[key].get<decltype(defaultVal)>()            \
                         : defaultVal)

using json = nlohmann::json;

// Internal typedefs for cleaner map signatures
using IDMap = std::unordered_map<std::string, int>;

/**********************************************************/
bool SceneLoader::load(const std::string &filepath, SceneData &outScene)
/**********************************************************/
{
  // SCOPED_TIMER_FUNC();
  std::ifstream file(filepath);
  if (!file.is_open()) {
    LOGE("[SceneLoader] Error: Could not open file {}", filepath.c_str());
    return false;
  } else {
  }

  // Clear any old data
  outScene.clear();

  // Local lookup maps to resolve names to indices during loading
  IDMap meshMap;
  IDMap texMap;
  IDMap matMap;
  try {
    json j;
    file >> j;

    // 1. Assets (Populates meshMap and texMap)
    if (j.contains("assets")) {
      parseAssets(j["assets"], outScene, meshMap, texMap);
    }

    // 2. Materials (Uses texMap, populates matMap)
    if (j.contains("materials")) {
      parseMaterials(j["materials"], outScene, texMap, matMap);
    }

    // 3. Instances (Uses meshMap and matMap)
    if (j.contains("instances")) {
      parseInstances(j["instances"], outScene, meshMap, matMap);
    }

    // 4. Global Scene Info
    if (j.contains("sceneInfo")) {
      parseSceneInfo(j["sceneInfo"], outScene);
    }

  } catch (const json::parse_error &e) {
    LOGE("[SceneLoader] JSON Parse Error in {}: {}", filepath.c_str(),
         e.what());
    return false;
  }

  return true;
}

/**********************************************************/
void SceneLoader::parseAssets(const json &j, SceneData &scene, IDMap &meshMap,
                              IDMap &texMap)
/**********************************************************/
{
  // 1. Meshes
  if (j.contains("meshes")) {
    for (auto &[key, val] : j["meshes"].items()) {
      std::string path = val.get<std::string>();
      int id = scene.addMesh(key, path);
      meshMap[key] = id;
    }
  }

  // 2. Textures
  if (j.contains("textures")) {
    for (auto &[key, val] : j["textures"].items()) {
      std::string path = val.get<std::string>();
      int id = scene.addTexture(key, path);
      texMap[key] = id;
    }
  }
}

/**********************************************************/
void SceneLoader::parseMaterials(const json &j, SceneData &scene,
                                 const IDMap &texMap, IDMap &matMap)
/**********************************************************/
{
  for (const auto &matJson : j) {
    DataMaterial mat;

    // Basic Properties
    mat.name = JSON_VAL(matJson, "id", std::string("Material"));
    mat.baseColor =
        parseVec4(matJson.value("baseColor", json::array()), glm::vec4(1.0f));
    mat.metallic = JSON_VAL(matJson, "metallic", 1.0f);
    mat.roughness = JSON_VAL(matJson, "roughness", 1.0f);
    mat.ior = parseVec3(matJson.value("ior", json::array()), glm::vec3(1.5f));

    // Texture Resolution
    if (matJson.contains("textureId")) {
      std::string texName = matJson["textureId"];

      auto it = texMap.find(texName);
      if (it != texMap.end()) {
        mat.textureIndex = it->second;
      } else {
        LOGW("[SceneLoader] Warning: Texture '{}' not found for material {}",
             texName.c_str(), mat.name.c_str());
        mat.textureIndex = -1;
      }
    }

    // Add to SceneData
    scene.materials.push_back(mat);

    // Store index for lookup (Material Name -> Index)
    matMap[mat.name] = static_cast<int>(scene.materials.size() - 1);
  }
}

/**********************************************************/
void SceneLoader::parseInstances(const json &j, SceneData &scene,
                                 const IDMap &meshMap, const IDMap &matMap)
/**********************************************************/
{
  for (const auto &instJson : j) {
    DataInstance inst;
    inst.name = JSON_VAL(instJson, "name", std::string("Instance"));

    // Resolve Mesh Index
    std::string meshName = JSON_VAL(instJson, "meshId", std::string(""));
    auto mIt = meshMap.find(meshName);
    if (mIt != meshMap.end()) {
      inst.meshIndex = mIt->second;
    } else {
      LOGW("[SceneLoader] Warning: Mesh '{}' not found for instance {}",
           meshName.c_str(), inst.name.c_str());
      continue; // Skip invalid instances
    }

    // Resolve Material Index
    std::string matName = JSON_VAL(instJson, "materialId", std::string(""));
    auto matIt = matMap.find(matName);
    if (matIt != matMap.end()) {
      inst.materialIndex = matIt->second;
    } else {
      inst.materialIndex = 0;
    }

    // Transforms
    if (instJson.contains("transform")) {
      const auto &t = instJson["transform"];
      inst.translation =
          parseVec3(t.value("translate", json::array()), glm::vec3(0.0f));
      inst.scale = parseVec3(t.value("scale", json::array()), glm::vec3(1.0f));
      inst.rotation =
          parseVec3(t.value("rotate", json::array()), glm::vec3(0.0f));
    }

    // Custom Properties
    std::string hitGroupStr =
        JSON_VAL(instJson, "hitGroup", std::string("Dielectrics"));
    inst.hitGroup = parseHitGroup(hitGroupStr);

    scene.instances.push_back(inst);
  }
}

/**********************************************************/
void SceneLoader::parseSceneInfo(const json &j, SceneData &scene)
/**********************************************************/
{
  scene.useSky = JSON_VAL(j, "useSky", false);

  if (j.contains("backgroundColor")) {
    scene.backgroundColor = parseVec3(j["backgroundColor"]);
  }

  // Camera
  if (j.contains("camera")) {
    const auto &c = j["camera"];
    scene.camera.eye =
        parseVec3(c.value("eye", json::array()), glm::vec3(0, 0, 5));
    scene.camera.center =
        parseVec3(c.value("center", json::array()), glm::vec3(0));
    scene.camera.up =
        parseVec3(c.value("up", json::array()), glm::vec3(0, 1, 0));
    scene.camera.clip =
        parseVec2(c.value("clip", json::array()), glm::vec2(0.01f, 100.0f));
  }

  // Lights
  if (j.contains("lights")) {
    for (const auto &l : j["lights"]) {
      DataLight light;
      light.position = parseVec3(l.value("position", json::array()));
      light.color = parseVec3(l.value("color", json::array()), glm::vec3(1.0f));
      light.intensity = JSON_VAL(l, "intensity", 1.0f);

      std::string typeStr = JSON_VAL(l, "type", std::string("Point"));
      light.type = parseLightType(typeStr);

      scene.lights.push_back(light);
    }
  }
}

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

/**********************************************************/
glm::vec3 SceneLoader::parseVec3(const json &j, const glm::vec3 &defaultValue)
/**********************************************************/
{
  if (j.is_array() && j.size() >= 3) {
    return glm::vec3(j[0], j[1], j[2]);
  }
  return defaultValue;
}

/**********************************************************/
glm::vec4 SceneLoader::parseVec4(const json &j, const glm::vec4 &defaultValue)
/**********************************************************/
{
  if (j.is_array() && j.size() >= 4) {
    return glm::vec4(j[0], j[1], j[2], j[3]);
  }
  return defaultValue;
}

/**********************************************************/
glm::vec2 SceneLoader::parseVec2(const json &j, const glm::vec2 &defaultValue)
/**********************************************************/
{
  if (j.is_array() && j.size() >= 2) {
    return glm::vec2(j[0], j[1]);
  }
  return defaultValue;
}

#include <stdexcept>
#include <string>

/**********************************************************/
MaterialType SceneLoader::parseHitGroup(const std::string &type)
/**********************************************************/
{
  if (type == "Mirror")
    return MaterialType::eMirror;
  if (type == "Dielectrics")
    return MaterialType::eDieletrics;
  if (type == "Gltf")
    return MaterialType::eGltfPbr;
  if (type == "Diffuse")
    return MaterialType::eDiffuse;
  if (type == "Normals")
    return MaterialType::eNormals;

  throw std::runtime_error("[SceneLoader] Unknown hit group type: " + type);
}

/**********************************************************/
shaderio::LightType SceneLoader::parseLightType(const std::string &type)
/**********************************************************/
{
  if (type == "Point")
    return shaderio::LightType::ePoint;
  if (type == "Directional")
    return shaderio::LightType::eDirectional;
  if (type == "Spot")
    return shaderio::LightType::eSpot;

  throw std::runtime_error("[SceneLoader] Unknown light type: " + type);
}
