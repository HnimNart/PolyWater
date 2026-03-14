#pragma once

#include <memory>
#include <string>

#include <nvvk/resources.hpp>

#include "Acceleration.hpp"
#include "SceneAssetManager.hpp"
#include "ShaderManager.hpp"
#include "interfaces/IRenderGraph.hpp"

#include "backend/vulkan/core/ContextManager.hpp"
#include "backend/vulkan/core/SwapchainRenderManager.hpp"

class PipelineManager {
public:
  struct BuildSettings {
    VulkanContextManager *context;
    VulkanSceneAssetManager *assetManager;
    SwapchainRenderManager *swapchainManager;
    ShaderManager *shaderManager;
    nvvk::Image *hiZTexture;
    AccelerationStructures *accel;
  };
  PipelineManager() = default;

  /**
   * @brief Factory method to create a fully configured RenderGraph
   * @param mode The string identifier (e.g., "Raster", "Meshlet", "Raytrace")
   */
  std::unique_ptr<RenderGraph> buildGraph(const BuildSettings &settings,
                                          const std::string &mode);

  std::vector<std::string> getAvaliableGraphs() const {
    return {"Raster", "Meshlet", "Raytrace"};
  }
};
