#include "Renderer.hpp"

#include "nvvk/check_error.hpp"
#include "nvvk/debug_util.hpp"
#include "nvvk/formats.hpp"
#include "nvvk/gbuffers.hpp"
#include "nvvk/helpers.hpp"

#include "SceneAssetManager.hpp"
#include "backend/vulkan/core/Backend.hpp"
#include "backend/vulkan/core/FrameSynchronizationManager.hpp"
#include "compiler/slang.hpp"
#include "core/timers.hpp"
#include "passes/MeshletPass.hpp"
#include "passes/MipReductionPass.hpp"
#include "passes/RasterPass.hpp"
#include "passes/SkyPass.hpp"
#include "passes/ToneMapPass.hpp"
#include "passes/UIPass.hpp"
#include "renderer/interfaces/IToneMapper.hpp"
#include "scene/SceneResources.hpp"
#include "shaders/shared/structs.h"

/**********************************************************/
VulkanRenderer::VulkanRenderer(VulkanBackend *backend,
                               const std::vector<std::filesystem::path> &paths)
/**********************************************************/
{
  SlangCompiler::instance().init(paths);
  m_context = backend->getContextManager();
  m_swapchain_manager = backend->getSwapchainManager();
  m_resources = std::make_shared<VulkanSceneAssetManager>(m_context);
  m_accel = AccelerationStructures::create(m_context);
  m_gBuffers = std::make_unique<nvvk::GBuffer>();

  initGBuffers();
  registerShaders();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**********************************************************/
void VulkanRenderer::init(const SceneResourcesManager & /*scene*/)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();
  buildGraph();
}

/**********************************************************/
void VulkanRenderer::clear()
/**********************************************************/
{
  m_context->waitForDeviceIdle();
  m_resources->clear();
  m_graph.deinit(m_context);
}

/**********************************************************/
void VulkanRenderer::deinit()
/**********************************************************/
{
  m_context->waitForDeviceIdle();
  m_post = nullptr;
  m_graph.deinit(m_context);
  m_gBuffers->deinit();
  m_accel.reset();
  m_resources->deinit();
  destroyHiZBuffer();
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
  m_shaderManager.registerMaterial(MaterialType::eEmissive, "emissive");
}

/**********************************************************/
void VulkanRenderer::reload()
/**********************************************************/
{
  m_context->waitForDeviceIdle();
  buildGraph();
}

/**********************************************************/
bool VulkanRenderer::update(const SceneResourcesManager &scene)
/**********************************************************/
{
  if (!scene.requireRebuild() && !scene.dirty()) {
    return false;
  }

  if (m_render_mode == RenderMode::RAYTRACE) {
    if (scene.requireRebuild()) {
      m_accel->clear();
      m_accel->build(scene, m_shaderManager);
    } else {
      m_accel->rebuild(scene, m_shaderManager);
    }
  }
  return true;
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
    // Traditional Vertex/Index pipeline
    m_graph.addPass(std::make_unique<SkyPass>());
    m_graph.addPass(
        std::make_unique<RasterPass>(m_resources->getDesriptorPack()));
  } else if (m_render_mode == RenderMode::MESHLET) {
    // Mesh Shader pipeline
    m_graph.addPass(std::make_unique<SkyPass>());
    m_graph.addPass(
        std::make_unique<MeshletPass>(m_resources->getDesriptorPack()));
    m_graph.addPass(std::make_unique<MipReductionPass>(&m_hiZTexture));
  } else if (m_render_mode == RenderMode::RAYTRACE) {
    // Ray Tracing pipeline
    m_graph.addPass(std::make_unique<RayTracePass>(
        m_resources->getDesriptorPack(), &m_shaderManager));
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
  vkCtx.hiZTexture = &m_hiZTexture;

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
  vkCtx.pushValues.screenResolution = {m_gBuffers->getSize().width,
                                       m_gBuffers->getSize().height};

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
  initHiZBuffer(cmd, {size.width, size.height});
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

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

/**********************************************************/
int64_t VulkanRenderer::getImageDescriptor(RenderOutput output) const
/**********************************************************/
{
  return reinterpret_cast<int64_t>(m_gBuffers->getDescriptorSet(output));
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

/**********************************************************/
void VulkanRenderer::initHiZBuffer(VkCommandBuffer cmd, VkExtent2D size)
/**********************************************************/
{
  destroyHiZBuffer();

  // Calculate full mip chain down to 1x1
  uint32_t mipLevels = static_cast<uint32_t>(std::floor(
                           std::log2(std::max(size.width, size.height)))) +
                       1;

  // Setup Image Info
  VkImageCreateInfo imageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = VK_FORMAT_R32_SFLOAT;
  imageInfo.extent = {size.width, size.height, 1};
  imageInfo.mipLevels = mipLevels;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  // Setup Image View Info
  VkImageViewCreateInfo viewInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = imageInfo.format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = mipLevels;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  // Create the Image via Allocator
  NVVK_CHECK(
      m_context->getAllocator().createImage(m_hiZTexture, imageInfo, viewInfo));
  NVVK_DBG_NAME(m_hiZTexture.image);

  // Create the strictly NEAREST sampler
  VkSamplerCreateInfo samplerInfo = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  samplerInfo.magFilter = VK_FILTER_NEAREST;
  samplerInfo.minFilter = VK_FILTER_NEAREST;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = static_cast<float>(mipLevels);

  NVVK_CHECK(vkCreateSampler(m_context->getDevice(), &samplerInfo, nullptr,
                             &m_hiZTexture.descriptor.sampler));

  // --- Transition Layout Immediately ---
  if (cmd != VK_NULL_HANDLE) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_hiZTexture.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    // Using explicitly calculated mipLevels is safer than
    // VK_REMAINING_MIP_LEVELS
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    barrier.srcAccessMask = 0;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

    VkDependencyInfo depInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cmd, &depInfo);

    // Update tracking to reflect the new layout
    m_hiZTexture.descriptor.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  } else {
    m_hiZTexture.descriptor.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  }
}

/**********************************************************/
void VulkanRenderer::destroyHiZBuffer()
/**********************************************************/
{
  if (m_hiZTexture.image != VK_NULL_HANDLE) {
    vkDestroySampler(m_context->getDevice(), m_hiZTexture.descriptor.sampler,
                     nullptr);
    m_context->getAllocator().destroyImage(m_hiZTexture);
    m_hiZTexture = {};
  }
}
