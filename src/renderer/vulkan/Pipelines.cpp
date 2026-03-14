#include "Pipelines.h"

#include <algorithm>
#include <memory>
#include <stdexcept>

// default passes
#include "passes/MeshletPass.hpp"
#include "passes/MipReductionPass.hpp"
#include "passes/RasterPass.hpp"
#include "passes/RayTracePass.hpp"
#include "passes/SkyPass.hpp"
#include "passes/ToneMapPass.hpp"
#include "passes/UIPass.hpp"

/**********************************************************/
PipelineManager::PipelineManager()
/**********************************************************/
{
  // ---------------------------------------------------------
  // 1. Raster Pipeline
  // ---------------------------------------------------------
  registerPipeline("Raster", [](const BuildSettings &settings) {
    auto graph = std::make_unique<RenderGraph>("Raster");
    auto &descriptorPack = settings.assetManager->getDesriptorPack();

    graph->addPass(std::make_unique<SkyPass>());
    graph->addPass(
        std::make_unique<RasterPass>(descriptorPack, settings.assetManager));
    graph->addPass(std::make_unique<ToneMapPass>());

    if (settings.swapchainManager) {
      graph->addPass(
          std::make_unique<UIPass>(settings.swapchainManager->getUICallback()));
    }
    return graph;
  });

  // ---------------------------------------------------------
  // 2. Meshlet Pipeline
  // ---------------------------------------------------------
  registerPipeline("Meshlet", [](const BuildSettings &settings) {
    auto graph = std::make_unique<RenderGraph>("Meshlet");
    auto &descriptorPack = settings.assetManager->getDesriptorPack();

    graph->addPass(std::make_unique<SkyPass>());
    graph->addPass(
        std::make_unique<MeshletPass>(descriptorPack, settings.hiZTexture));
    graph->addPass(std::make_unique<MipReductionPass>(settings.hiZTexture));
    graph->addPass(std::make_unique<ToneMapPass>());

    if (settings.swapchainManager) {
      graph->addPass(
          std::make_unique<UIPass>(settings.swapchainManager->getUICallback()));
    }
    return graph;
  });

  // ---------------------------------------------------------
  // 3. Raytrace Pipeline
  // ---------------------------------------------------------
  registerPipeline("Raytrace", [](const BuildSettings &settings) {
    auto graph = std::make_unique<RenderGraph>("Raytrace");
    auto &descriptorPack = settings.assetManager->getDesriptorPack();

    graph->addPass(std::make_unique<RayTracePass>(
        descriptorPack, settings.shaderManager, settings.accel));
    graph->addPass(std::make_unique<ToneMapPass>());

    if (settings.swapchainManager) {
      graph->addPass(
          std::make_unique<UIPass>(settings.swapchainManager->getUICallback()));
    }
    return graph;
  });
}

/**********************************************************/
void PipelineManager::registerPipeline(const std::string &mode,
                                       PipelineFactoryFunc factory)
/**********************************************************/
{
  // Optional: Warn if overwriting an existing pipeline
  m_registry[mode] = std::move(factory);

  // Update the cache immediately
  m_availableGraphsCache.clear();
  m_availableGraphsCache.reserve(m_registry.size());

  for (const auto &[name, _] : m_registry) {
    m_availableGraphsCache.push_back(name);
  }

  std::sort(m_availableGraphsCache.begin(), m_availableGraphsCache.end());
}

/**********************************************************/
std::unique_ptr<RenderGraph>
PipelineManager::buildGraph(const BuildSettings &settings,
                            const std::string &mode) const
/**********************************************************/
{
  auto it = m_registry.find(mode);
  if (it == m_registry.end()) {
    throw std::runtime_error("PipelineManager: Requested RenderMode '" + mode +
                             "' is not registered.");
  }

  // Call the registered factory function to build the graph
  return it->second(settings);
}

/**********************************************************/
const std::vector<std::string> &PipelineManager::getAvailableGraphs() const
/**********************************************************/
{
  return m_availableGraphsCache;
}
