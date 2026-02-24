#include "SceneSaver.hpp"
#include "core/logger.hpp"
#include <fstream>

#include "shaders/shared/structs.h"

using json = nlohmann::json;

namespace {
// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

/**********************************************************/
json vec2ToJson(const glm::vec2 &v)
/**********************************************************/
{
  return {v.x, v.y};
}

/**********************************************************/
json vec3ToJson(const glm::vec3 &v)
/**********************************************************/
{
  return {v.x, v.y, v.z};
}

/**********************************************************/
json vec4ToJson(const glm::vec4 &v)
/**********************************************************/
{
  return {v.x, v.y, v.z, v.w};
}

/**********************************************************/
std::string hitGroupToString(MaterialType type)
/**********************************************************/
{
  switch (type) {
  case MaterialType::eMirror:
    return "Mirror";
  case MaterialType::eDieletrics:
    return "Dielectrics"; // Note: matches your typo in loader!
  case MaterialType::eGltfPbr:
    return "Gltf";
  case MaterialType::eDiffuse:
    return "Diffuse";
  case MaterialType::eVolumetric:
    return "Volumetric";
  case MaterialType::eNormals:
    return "Normals";
  case MaterialType::eEmissive:
    return "Emissive";
  default:
    return "Dielectrics";
  }
}

/**********************************************************/
std::string lightTypeToString(shaderio::LightType type)
/**********************************************************/
{
  switch (type) {
  case shaderio::LightType::ePoint:
    return "Point";
  case shaderio::LightType::eDirectional:
    return "Directional";
  case shaderio::LightType::eSpot:
    return "Spot";
  default:
    return "Point";
  }
}

/**********************************************************/
json serializeAssets(const SceneData &scene)
/**********************************************************/
{
  json j;

  // Assuming SceneData has maps for these: std::unordered_map<std::string,
  // std::string>
  if (!scene.meshPaths.empty()) {
    j["meshes"] = scene.meshPaths;
  }

  if (!scene.texturePaths.empty()) {
    j["textures"] = scene.texturePaths;
  }

  return j;
}

/**********************************************************/
json serializeMaterials(const SceneData &scene)
/**********************************************************/
{
  json j = json::array();

  for (const auto &mat : scene.materials) {
    json m;
    m["id"] = mat.name;
    m["baseColor"] = vec4ToJson(mat.baseColor);
    m["metallic"] = mat.metallic;
    m["roughness"] = mat.roughness;
    m["emission"] = vec3ToJson(mat.emission);
    m["ior"] = vec3ToJson(mat.ior);

    if (!mat.textureId.empty()) {
      m["textureId"] = mat.textureId;
    }

    j.push_back(m);
  }

  return j;
}

/**********************************************************/
json serializeInstances(const SceneData &scene)
/**********************************************************/
{
  json j = json::array();

  for (const auto &inst : scene.instances) {
    json i;
    i["name"] = inst.name;
    i["meshId"] = inst.meshId;
    i["materialId"] = inst.materialId;
    i["hitGroup"] = hitGroupToString(inst.hitGroup);

    json transform;
    transform["translate"] = vec3ToJson(inst.translation);
    transform["scale"] = vec3ToJson(inst.scale);
    transform["rotate"] = vec3ToJson(inst.rotation);
    i["transform"] = transform;

    j.push_back(i);
  }

  return j;
}

/**********************************************************/
json serializeSceneInfo(const SceneData &scene)
/**********************************************************/
{
  json j;

  j["useSky"] = scene.useSky;
  j["backgroundColor"] = vec3ToJson(scene.backgroundColor);

  // Camera
  json cam;
  cam["eye"] = vec3ToJson(scene.camera.eye);
  cam["center"] = vec3ToJson(scene.camera.center);
  cam["up"] = vec3ToJson(scene.camera.up);
  cam["clip"] = vec2ToJson(scene.camera.clip);
  j["camera"] = cam;

  // Environment Map
  if (scene.envmap.useEnvMap && !scene.envmap.path.empty()) {
    json env;
    env["file"] = scene.envmap.path;
    env["scale"] = scene.envmap.scale;
    env["rotation"] = scene.envmap.rotation;
    j["envmap"] = env;
  }

  // Lights
  if (!scene.lights.empty()) {
    json lights = json::array();
    for (const auto &light : scene.lights) {
      json l;
      l["type"] = lightTypeToString(light.type);
      l["position"] = vec3ToJson(light.position);
      l["color"] = vec3ToJson(light.color);
      l["intensity"] = light.intensity;
      lights.push_back(l);
    }
    j["lights"] = lights;
  }

  return j;
}

} // namespace

/**********************************************************/
bool SceneSaver::save(const std::string &filepath, const SceneData &scene)
/**********************************************************/
{
  json j;

  // Build the JSON structure step by step
  j["assets"] = serializeAssets(scene);
  j["materials"] = serializeMaterials(scene);
  j["instances"] = serializeInstances(scene);
  j["sceneInfo"] = serializeSceneInfo(scene);

  // Write to file
  std::ofstream file(filepath);
  if (!file.is_open()) {
    LOGE("[SceneSaver] Error: Could not open file for writing: %s\n",
         filepath.c_str());
    return false;
  }

  // Dump with an indent of 2 spaces for readability
  file << j.dump(2);
  LOGI("[SceneSaver] Successfully saved scene to %s", filepath.c_str());

  return true;
}
