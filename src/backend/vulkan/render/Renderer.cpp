#include "Renderer.hpp"

#include <nvvk/check_error.hpp>
#include <nvvk/commands.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/formats.hpp>
#include <nvvk/gbuffers.hpp>
#include <nvvk/helpers.hpp>

#include "backend/interfaces/IToneMapper.hpp"
#include "backend/vulkan/core/Backend.hpp"
#include "backend/vulkan/core/FrameSynchronizationManager.hpp"
#include "backend/vulkan/render/SceneAssetManager.hpp"
#include "common/timers.hpp"
#include "passes/RasterPass.hpp"
#include "passes/SkyPass.hpp"
#include "passes/ToneMapPass.hpp"
#include "passes/UIPass.hpp"
#include "scene/SceneResources.hpp"
#include "shaders/shared/structs.h"

/**********************************************************/
VulkanRenderer::VulkanRenderer(VulkanBackend *backend)
/**********************************************************/
{
  m_context = backend->getContextManager();
  m_swapchain_manager = backend->getSwapchainManager();
  m_resources = std::make_shared<VulkanSceneAssetManager>(m_context);
  m_gBuffers = std::make_unique<nvvk::GBuffer>();
  m_accel = AccelerationStructures::create(m_context);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**********************************************************/
void VulkanRenderer::init(const SceneResourcesManager &scene)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();
  initGBuffers();
  createDescriptorSetLayout(m_context->getDevice());
  registerShaders();
  buildGraph();
  m_accel->build(scene, m_shaderManager);
  m_resources->updateSceneResources();
}

/**********************************************************/
void VulkanRenderer::deinit()
/**********************************************************/
{
  m_context->waitForDeviceIdle();
  m_post = nullptr;
  m_graph.deinit(m_context);
  m_accel.reset();

  m_descPack.deinit();
  m_gBuffers->deinit();
  m_resources->deinit();
}

/**********************************************************/
void VulkanRenderer::registerShaders()
/**********************************************************/
{
  m_shaderManager.registerMaterial(MaterialType::eDiffuse, "diffuse");
  m_shaderManager.registerMaterial(MaterialType::eGltfPbr, "gltf");
  m_shaderManager.registerMaterial(MaterialType::eNormals, "normals");
  m_shaderManager.registerMaterial(MaterialType::eDieletrics, "dielectric");
  m_shaderManager.registerMaterial(MaterialType::eMirror, "mirror");
  m_shaderManager.registerMaterial(MaterialType::eVolumetric, "volumetric");
}

/**********************************************************/
void VulkanRenderer::reload()
/**********************************************************/
{
  m_context->waitForDeviceIdle();
  buildGraph();
}

/**********************************************************/
void VulkanRenderer::update(const SceneResourcesManager &scene)
/**********************************************************/
{
  if (scene.dirty() && m_render_mode == RenderMode::RAYTRACE) {
    m_accel->rebuild(scene, m_shaderManager);
    reset();
  }
}

/**********************************************************/
void VulkanRenderer::setRenderMode(RenderMode mode)
/**********************************************************/
{
  if (m_render_mode != mode) {
    m_context->waitForDeviceIdle();
    m_render_mode = mode;
    buildGraph();
    reset();
  }
}

/**********************************************************/
void VulkanRenderer::buildGraph()
/**********************************************************/
{
  SCOPED_TIMER_FUNC();

  // 1. Clear existing passes
  m_graph.deinit(m_context);

  // 2. Add passes based on mode
  if (m_render_mode == RenderMode::RASTER) {
    // Raster Configuration: Sky -> Geometry -> ToneMap
    m_graph.addPass(std::make_unique<SkyPass>());
    m_graph.addPass(std::make_unique<RasterPass>(&m_descPack));
  } else {
    m_graph.addPass(
        std::make_unique<RayTracePass>(&m_descPack, &m_shaderManager));
  }

  // Common: Post Processing
  auto tonePass = std::make_unique<ToneMapPass>();
  m_post = tonePass.get(); // Cache pointer for UI access
  m_graph.addPass(std::move(tonePass));

  if (m_swapchain_manager) {
    m_graph.addPass(
        std::make_unique<UIPass>(m_swapchain_manager->getUICallback()));
  }

  m_graph.init(m_context);
  m_graph.compile();
}

/**********************************************************/
void VulkanRenderer::render(IRenderContext &ctx)
/**********************************************************/
{
  auto &vkCtx = VulkanRenderContext::get(ctx);

  // 1. Link Core Subsystems
  vkCtx.gBuffers = m_gBuffers.get();
  vkCtx.bvh = m_accel.get();
  vkCtx.assetManager = m_resources.get();

  // 2. Update GPU Resources (Uploads & Barriers)
  auto *sceneInfoAddress =
      m_resources->update(vkCtx.cmdBuffer, vkCtx.sceneResources->sceneInfo);
  auto *resourcesAddress = m_resources->getSceneResources();

  // 3. Configure Frame Global State (Push Constants)
  vkCtx.pushValues.sceneInfoAddress = sceneInfoAddress;
  vkCtx.pushValues.resourcesAddress = resourcesAddress;
  vkCtx.pushValues.renderParams = m_renderParams;
  vkCtx.pushValues.renderParams.frameIdx = m_frameIndex;
  vkCtx.pushValues.rasterParams = m_rasterParams;

  // 4. Setup Render Targets (Swapchain)
  if (m_swapchain_manager) {
    const auto &swapchain = m_swapchain_manager->getSwapchain();

    vkCtx.swapchainImage = swapchain.getImage();
    vkCtx.swapchainImageView = swapchain.getImageView();
    vkCtx.screenSize = m_swapchain_manager->getWindowSize();
  }

  // 5. Execute Render Graph
  m_graph.execute(ctx);

  m_frameIndex++;
}

/**********************************************************/
void VulkanRenderer::onResize(const WindowSize &size)
/**********************************************************/
{
  m_context->waitForDeviceIdle();
  VkCommandBuffer cmd = m_context->startSingleTimeCmd();
  NVVK_CHECK(m_gBuffers->update(cmd, {size.width, size.height}));
  m_context->endSingleTimeCmd(cmd);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**********************************************************/
void VulkanRenderer::initGBuffers()
/**********************************************************/
{
  VkSampler linearSampler{};
  NVVK_CHECK(m_resources->samplerPool().acquireSampler(linearSampler));
  NVVK_DBG_NAME(linearSampler);

  nvvk::GBufferInitInfo info{
      .allocator = &m_context->getAllocator(),
      .colorFormats = {VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM,
                       VK_FORMAT_R32G32B32A32_SFLOAT},
      .depthFormat = nvvk::findDepthFormat(m_context->getPhysicalDevice()),
      .imageSampler = linearSampler,
      .descriptorPool = m_context->getDescriptorPool()};

  m_gBuffers->init(info);
}

/**********************************************************/
void VulkanRenderer::createDescriptorSetLayout(VkDevice device)
/**********************************************************/
{
  nvvk::DescriptorBindings bindings;
  bindings.addBinding(
      {.binding = shaderio::BindingPoints::eTextures,
       .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       .descriptorCount = 10, // TODO this sohuld benumber of textures?
       .stageFlags = VK_SHADER_STAGE_ALL},
      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
          VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
          VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);

  m_descPack.init(bindings, device, 1,
                  VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                  VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT |
                      VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);

  NVVK_DBG_NAME(m_descPack.getLayout());
  NVVK_DBG_NAME(m_descPack.getPool());
  NVVK_DBG_NAME(m_descPack.getSet(0));
  m_resources->updateDescriptors(m_descPack);
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

/**********************************************************/
void *VulkanRenderer::getImageDescriptor(RenderOutput output) const
/**********************************************************/
{
  return static_cast<void *>(m_gBuffers->getDescriptorSet(output));
}

/**********************************************************/
IToneMapper &VulkanRenderer::postProcessor() noexcept
/**********************************************************/
{
  return *m_post;
}

/**********************************************************/
std::shared_ptr<IDeviceAssets> VulkanRenderer::deviceResources() noexcept
/**********************************************************/
{
  return m_resources;
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

/**********************************************************/
void VulkanRenderer::saveImage(const std::filesystem::path &filename,
                               int quality) const
/**********************************************************/
{
  VkDevice device = m_context->getDevice();
  VkPhysicalDevice physicalDevice = m_context->getPhysicalDevice();
  VkImage dstImage = {};
  VkDeviceMemory dstImageMemory = {};
  VkCommandBuffer cmd = m_context->startSingleTimeCmd();

  VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
  if (filename.extension() == ".hdr") {
    format = VK_FORMAT_R32G32B32A32_SFLOAT;
  }

  auto srcImage = m_gBuffers->getColorImage(RenderOutput::ToneMapped);
  VkExtent2D size = m_gBuffers->getSize();
  nvvk::imageToLinear(cmd, device, physicalDevice, srcImage, size, dstImage,
                      dstImageMemory, format);

  m_context->endSingleTimeCmd(cmd);
  nvvk::saveImageToFile(device, dstImage, dstImageMemory, size, filename,
                        quality);

  // Clean up resources
  vkUnmapMemory(device, dstImageMemory);
  vkFreeMemory(device, dstImageMemory, nullptr);
  vkDestroyImage(device, dstImage, nullptr);
}
