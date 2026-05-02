#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nvvk/resources.hpp>

#include "backend/vulkan/core/vulkan_context_manager.hpp"
#include "backend/vulkan/core/vulkan_swapchain_render_manager.hpp"
#include "interfaces/render_graph_interface.hpp"
#include "shader_manager.hpp"
#include "vulkan_acceleration_structures.hpp"
#include "vulkan_scene_asset_manager.hpp"

class VulkanPipelineManager
{
public:
  struct BuildSettings
  {
    VulkanContextManager* context;
    VulkanSceneAssetManager* assetManager;
    VulkanSwapchainRenderManager* swapchainManager;
    ShaderManager* shaderManager;
    nvvk::Image* hiZTexture;
    VulkanAccelerationStructures* accel;
    bool denoise;
  };

  // Define the signature for functions that build render graphs
  using PipelineFactoryFunc =
      std::function<std::unique_ptr<RenderGraph>(const BuildSettings&)>;

  VulkanPipelineManager();

  /**
   * @brief Registers a new pipeline layout under a string identifier.
   * @param mode The string identifier (e.g., "Raster")
   * @param factory The lambda or function that constructs the graph
   */
  void registerPipeline(const std::string& mode, PipelineFactoryFunc factory);

  /**
   * @brief Factory method to create a fully configured RenderGraph
   * @param mode The string identifier (e.g., "Raster", "Meshlet", "Raytrace")
   */
  std::unique_ptr<RenderGraph> buildGraph(const BuildSettings& settings,
                                          const std::string& mode) const;

  /**
   * @brief Returns a list of all currently registered pipeline names.
   */
  const std::vector<std::string>& getAvailableGraphs() const;

private:
  std::unordered_map<std::string, PipelineFactoryFunc> m_registry;
  std::vector<std::string> m_availableGraphsCache;
};
