#include "vulkan_pipeline_manager.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>

// default passes
#include "renderer/vulkan/passes/vulkan_denoise_pass.hpp"
#include "renderer/vulkan/passes/vulkan_meshlet_pass.hpp"
#include "renderer/vulkan/passes/vulkan_mip_reduction_pass.hpp"
#include "renderer/vulkan/passes/vulkan_raster_pass.hpp"
#include "renderer/vulkan/passes/vulkan_ray_trace_pass.hpp"
#include "renderer/vulkan/passes/vulkan_sky_pass.hpp"
#include "renderer/vulkan/passes/vulkan_tone_map_pass.hpp"
#include "renderer/vulkan/passes/vulkan_ui_pass.hpp"

/**********************************************************/
VulkanPipelineManager::VulkanPipelineManager()
/**********************************************************/
{
  // ---------------------------------------------------------
  // 1. Raster Pipeline
  // ---------------------------------------------------------
  registerPipeline(
      "Raster",
      [](const BuildSettings& settings)
      {
        auto graph = std::make_unique<RenderGraph>("Raster");
        auto& descriptorPack = settings.assetManager->getDesriptorPack();

        graph->addPass(std::make_unique<VulkanSkyPass>(settings.context));
        graph->addPass(std::make_unique<VulkanRasterPass>(
            settings.context, descriptorPack, settings.assetManager));
        graph->addPass(std::make_unique<VulkanToneMapPass>(
            settings.context, RenderOutput::Linear));

        if (settings.swapchainManager)
        {
          graph->addPass(std::make_unique<VulkanUIPass>(
              settings.swapchainManager->getUICallback()));
        }
        return graph;
      });

  // ---------------------------------------------------------
  // 2. Meshlet Pipeline
  // ---------------------------------------------------------
  registerPipeline(
      "Meshlet",
      [](const BuildSettings& settings)
      {
        auto graph = std::make_unique<RenderGraph>("Meshlet");
        auto& descriptorPack = settings.assetManager->getDesriptorPack();

        graph->addPass(std::make_unique<VulkanSkyPass>(settings.context));
        graph->addPass(std::make_unique<VulkanMeshletPass>(
            settings.context, descriptorPack, settings.hiZTexture));
        graph->addPass(std::make_unique<VulkanMipReductionPass>(
            settings.context, settings.hiZTexture));
        graph->addPass(std::make_unique<VulkanToneMapPass>(
            settings.context, RenderOutput::Linear));

        if (settings.swapchainManager)
        {
          graph->addPass(std::make_unique<VulkanUIPass>(
              settings.swapchainManager->getUICallback()));
        }
        return graph;
      });

  registerPipeline(
      "Raytrace",
      [](const BuildSettings& settings)
      {
        auto graph = std::make_unique<RenderGraph>("Raytrace");
        auto& descriptorPack = settings.assetManager->getDesriptorPack();

        // 1. Trace the rays (outputs noisy HDR image to RenderOutput::Linear)
        graph->addPass(std::make_unique<VulkanRayTracePass>(
            settings.context, descriptorPack, settings.shaderManager,
            settings.accel));

        // 2. Denoise the image
        graph->addPass(std::make_unique<VulkanDenoisePass>(settings.context));

        // 3. Tone Mapping
        graph->addPass(std::make_unique<VulkanToneMapPass>(
            settings.context, RenderOutput::Denoised));

        // 4. UI Layer
        if (settings.swapchainManager)
        {
          graph->addPass(std::make_unique<VulkanUIPass>(
              settings.swapchainManager->getUICallback()));
        }
        return graph;
      });
}

/**********************************************************/
void VulkanPipelineManager::registerPipeline(const std::string& mode,
                                             PipelineFactoryFunc factory)
/**********************************************************/
{
  // Optional: Warn if overwriting an existing pipeline
  m_registry[mode] = std::move(factory);

  // Update the cache immediately
  m_availableGraphsCache.clear();
  m_availableGraphsCache.reserve(m_registry.size());

  for (const auto& [name, _] : m_registry)
  {
    m_availableGraphsCache.push_back(name);
  }

  std::sort(m_availableGraphsCache.begin(), m_availableGraphsCache.end());
}

/**********************************************************/
std::unique_ptr<RenderGraph>
VulkanPipelineManager::buildGraph(const BuildSettings& settings,
                                  const std::string& mode) const
/**********************************************************/
{
  auto it = m_registry.find(mode);
  if (it == m_registry.end())
  {
    throw std::runtime_error("VulkanPipelineManager: Requested RenderMode '" +
                             mode + "' is not registered.");
  }

  // Call the registered factory function to build the graph
  return it->second(settings);
}

/**********************************************************/
const std::vector<std::string>&
VulkanPipelineManager::getAvailableGraphs() const
/**********************************************************/
{
  return m_availableGraphsCache;
}
