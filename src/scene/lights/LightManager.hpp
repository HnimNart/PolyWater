#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "renderer/interfaces/IDeviceAssets.hpp"
#include "scene/Scene.h"
#include "scene/SceneData.hpp"
#include "shaders/shared/structs.h"
#include "shaders/shared/tonemapper_io.h.slang"

class LightManager {
public:
  LightManager() = default;

  /**
   * Extracts emissive geometry from the scene and uploads to GPU.
   */
  shaderio::AreaLight
  uploadAreaLights(const Scene &scene,
                   const std::shared_ptr<IDeviceAssets> &deviceResources);

  /**
   * Extracts analytic lights (Point, Spot, etc.) from SceneData.
   */
  void uploadPointLights(const Scene &scene);

  /**
   * Loads envmap buffer to CPU on disk
   */
  EnvmapInfo loadEnvmap(const DataEnvmap &data);

  // Uploads to GPU
  shaderio::EnvmapLight
  uploadEnvmap(const Scene &scene,
               const std::shared_ptr<IDeviceAssets> &deviceResources);

private:
  /**
   * Scans the scene graph for emissive materials and calculates
   * the importance (power) for the CDF.
   */
  std::pair<std::vector<float>, std::vector<shaderio::TriangleLight>>
  extractAreaLights(const Scene &scene);
};
