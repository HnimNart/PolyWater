#include "Pipelines.h"

#include "passes/MeshletPass.hpp"
#include "passes/MipReductionPass.hpp"
#include "passes/RasterPass.hpp"
#include "passes/RayTracePass.hpp"
#include "passes/SkyPass.hpp"
#include "passes/ToneMapPass.hpp"
#include "passes/UIPass.hpp"

/**********************************************************/
std::unique_ptr<RenderGraph>
PipelineManager::buildGraph(const BuildSettings &settings,
                            const std::string &mode)
/**********************************************************/
{
  auto available = getAvaliableGraphs();
  if (std::find(available.begin(), available.end(), mode) == available.end()) {
    throw std::runtime_error("Requested RenderMode '" + mode +
                             "' is not supported by PipelineManager.");
  }
  auto graph = std::make_unique<RenderGraph>(mode);
  auto &descriptorPack = settings.assetManager->getDesriptorPack();

  // Geometry / Primary Visibility Stage
  if (mode == "Raster") {
    graph->addPass(std::make_unique<SkyPass>());
    graph->addPass(
        std::make_unique<RasterPass>(descriptorPack, settings.assetManager));
  } else if (mode == "Meshlet") {
    graph->addPass(std::make_unique<SkyPass>());
    graph->addPass(
        std::make_unique<MeshletPass>(descriptorPack, settings.hiZTexture));
    graph->addPass(std::make_unique<MipReductionPass>(settings.hiZTexture));
  } else if (mode == "Raytrace") {
    graph->addPass(std::make_unique<RayTracePass>(
        descriptorPack, settings.shaderManager, settings.accel));
  }

  // Post-Processing (Common to all modes)
  auto tonePass = std::make_unique<ToneMapPass>();
  graph->addPass(std::move(tonePass));

  // UI Layer
  if (settings.swapchainManager) {
    graph->addPass(
        std::make_unique<UIPass>(settings.swapchainManager->getUICallback()));
  }

  return graph;
}
