#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nvvk/resources.hpp>

#include "acceleration.hpp"
#include "scene_asset_manager.hpp"
#include "shader_manager.hpp"
#include "backend/vulkan/core/context_manager.hpp"
#include "backend/vulkan/core/swapchain_render_manager.hpp"
#include "interfaces/render_graph_interface.hpp"

class PipelineManager
{
public:
  struct BuildSettings
  {
    VulkanContextManager* context;
    VulkanSceneAssetManager* assetManager;
    SwapchainRenderManager* swapchainManager;
    ShaderManager* shaderManager;
    nvvk::Image* hiZTexture;
    AccelerationStructures* accel;
  };

  // Define the signature for functions that build render graphs
  using PipelineFactoryFunc =
      std::function<std::unique_ptr<RenderGraph>(const BuildSettings&)>;

  PipelineManager();

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
