#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "shaders/shared/structs.h"

// Lightweight structs to hold Scene data temporarily
struct DataMesh
{
  std::string name;
  std::string path;
};

struct DataTexture
{
  std::string name;
  std::string path;
};

struct DataMaterial
{
  std::string name;
  glm::vec4 baseColor{1.0f};
  float metallic{1.0f};
  float roughness{1.0f};
  glm::vec3 emission{0.0f};
  glm::vec3 ior{1.5f};
  std::string textureId = "";  // name of texture
};

struct DataInstance
{
  std::string name;
  std::string meshId = "";  // name of mesh
  std::string materialId = "";
  glm::vec3 translation{0.0f};
  glm::vec3 scale{1.0f};
  glm::vec3 rotation{0.0f};
  MaterialType hitGroup = MaterialType::eDieletrics;
};

struct DataLight
{
  shaderio::LightType type;
  glm::vec3 position;
  glm::vec3 color;
  float intensity;
};

struct DataEnvmap
{
  std::string path;
  float scale = 1.0f;
  float rotation = 0.0f;
  bool useEnvMap = false;
};

struct DataCamera
{
  glm::vec3 eye, center, up;
  glm::vec2 clip;
};

// --------------------------------------------------------
// The Intermediate Representation
// --------------------------------------------------------
struct SceneData
{
  // Assets to be loaded (files)
  std::vector<DataMesh> meshPaths;
  std::vector<DataTexture> texturePaths;

  // Definitions
  std::vector<DataMaterial> materials;
  std::vector<DataInstance> instances;
  std::vector<DataLight> lights;

  // Scene Globals
  DataEnvmap envmap;
  DataCamera camera;
  glm::vec3 backgroundColor{0.0f};
  bool useSky = false;

  void clear()
  {
    meshPaths.clear();
    texturePaths.clear();
    materials.clear();
    instances.clear();
    lights.clear();
  }

  int addMesh(const std::string& name, const std::string& path)
  {
    meshPaths.push_back({name, path});
    return (int) meshPaths.size() - 1;
  }
  int addTexture(const std::string& name, const std::string& path)
  {
    texturePaths.push_back({name, path});
    return (int) texturePaths.size() - 1;
  }

  void dump() const;
};
