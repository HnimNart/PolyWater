#pragma once

#include <vector>

#include "shaders/shaderio.h"

struct Scene {
  std::vector<shaderio::MeshPrimitive> meshes{}; // All meshes in the scene
  std::vector<shaderio::Instance> instances;     // All instances in the scene
  std::vector<shaderio::Material> materials;     // All materials in the scene
  shaderio::SceneInfo
      sceneInfo; // Scene information (camera matrices and lights)
  shaderio::SceneResources
      sceneResources; // pointers to meshes, instances, materials
};
