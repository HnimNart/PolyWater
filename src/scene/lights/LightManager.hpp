#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "renderer/interfaces/IDeviceAssets.hpp"
#include "scene/Scene.h"
#include "scene/SceneData.hpp"
#include "shaders/shared/structs.h"

struct EnvmapInfo {
  float scale = 1.0f;
  float rotation = 0.0f;

  // CPU Raw Data
  core::Image image;

  // MIS Data (Calculated on CPU)
  std::vector<float> importanceMap;
  std::vector<float> cdfRows;
  std::vector<float> cdfCols;
  float totalIntegral;
};

class LightManager {
public:
  LightManager() = default;

  /**
   * Loads envmap buffer to CPU on disk
   */
  EnvmapInfo loadEnvmap(const std::filesystem::path &filename, float scale,
                        float rotation);

  /**
   * Extracts emissive geometry from the scene and uploads to GPU.
   */
  shaderio::AreaLight
  uploadAreaLights(const Scene &scene,
                   const std::shared_ptr<IDeviceAssets> &deviceResources);

  // Uploads to GPU
  shaderio::EnvmapLight
  uploadEnvmap(const EnvmapInfo &info,
               const std::shared_ptr<IDeviceAssets> &deviceResources);

  float computeAnalyticalLightContribution(const Scene &scene);

private:
  /**
   * Scans the scene graph for emissive materials and calculates
   * the importance (power) for the CDF.
   */
  std::pair<std::vector<float>, std::vector<shaderio::TriangleLight>>
  extractAreaLights(const Scene &scene);
};
