#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "renderer/interfaces/device_assets_interface.hpp"
#include "scene/scene.hpp"
#include "shaders/shared/structs.h"


namespace scene
{

struct EnvmapInfo
{
  std::string filename;
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

class LightManager
{
public:
  LightManager() = default;

  /**
   * Loads envmap buffer to CPU on disk
   */
  const EnvmapInfo& loadEnvmap(const std::filesystem::path& filename,
                               float scale, float rotation);

  /**
   * Extracts emissive geometry from the scene and uploads to GPU.
   */
  void uploadAreaLights(const Scene& scene,
                        const std::shared_ptr<IDeviceAssets>& deviceResources,
                        shaderio::AreaLight& areaLight);

  // Uploads to GPU
  void uploadEnvmap(const EnvmapInfo& info,
                    const std::shared_ptr<IDeviceAssets>& deviceResources,
                    shaderio::EnvmapLight& envmapLight);

  float computeAnalyticalLightContribution(const Scene& scene);

private:
  /**
   * Scans the scene graph for emissive materials and calculates
   * the importance (power) for the CDF.
   */
  std::pair<std::vector<float>, std::vector<shaderio::TriangleLight>>
  extractAreaLights(const Scene& scene);

  // cached members
  EnvmapInfo m_envmapInfo;
  std::string m_currentEnvFilename{};
};

}  // namespace scene
