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
#include "scene/gltf/io_gltf.h"

/**********************************************************/
VulkanRenderer::VulkanRenderer(VulkanBackend* backend)
/**********************************************************/
{
  m_context_manager = backend->getContextManager();
  m_swapchain_manager = backend->getSwapchainManager();
  m_resources = std::make_shared<VulkanSceneAssetManager>(m_context_manager);
  m_gBuffers = std::make_unique<nvvk::GBuffer>();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**********************************************************/
void VulkanRenderer::init(const SceneResourcesManager& scene)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();
  initGBuffers();
  createDescriptorSetLayout(m_context_manager->getDevice());
  m_resources->updateDescriptors(m_descPack);
  buildGraph(scene);
}

/**********************************************************/
void VulkanRenderer::deinit()
/**********************************************************/
{
  m_context_manager->waitForDeviceIdle();
  m_post = nullptr;

  m_graph.deinit(m_context_manager);
  m_accel.reset();

  m_descPack.deinit();
  m_gBuffers->deinit();
  m_resources->deinit();
}

/**********************************************************/
void VulkanRenderer::reload(const SceneResourcesManager& scene)
/**********************************************************/
{
  m_context_manager->waitForDeviceIdle();
  buildGraph(scene);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

/**********************************************************/
shaderio::GltfSceneInfo*
VulkanRenderer::updateSceneBuffer(VkCommandBuffer cmd,
                                  shaderio::GltfSceneInfo& sceneInfo) const
/**********************************************************/
{
  NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

  const VulkanSceneGpuData& device_resources = m_resources->deviceResources();
  sceneInfo.instances = (shaderio::GltfInstance*) device_resources.bInstances
                            .address;  // Get the address of the instance buffer
  sceneInfo.meshes = (shaderio::GltfMesh*) device_resources.bMeshes
                         .address;  // Get the address of the mesh buffer
  sceneInfo.materials =
      (shaderio::GltfMetallicRoughness*) device_resources.bMaterials
          .address;  // Get the address of the material buffer

  // Making sure the scene information buffer is updated before rendering
  nvvk::cmdBufferMemoryBarrier(cmd, {device_resources.bSceneInfo.buffer,
                                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                     VK_PIPELINE_STAGE_2_TRANSFER_BIT});
  vkCmdUpdateBuffer(cmd, device_resources.bSceneInfo.buffer, 0,
                    sizeof(shaderio::GltfSceneInfo), &sceneInfo);
  nvvk::cmdBufferMemoryBarrier(cmd, {device_resources.bSceneInfo.buffer,
                                     VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT});
  return reinterpret_cast<shaderio::GltfSceneInfo*>(
      device_resources.bSceneInfo.address);
}

/**********************************************************/
void VulkanRenderer::setRenderMode(RenderMode mode,
                                   const SceneResourcesManager& scene)
/**********************************************************/
{
  if (m_render_mode != mode)
  {
    m_context_manager->waitForDeviceIdle();
    m_render_mode = mode;
    buildGraph(scene);
  }
}

/**********************************************************/
void VulkanRenderer::buildGraph(const SceneResourcesManager& scene)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();

  // 1. Clear existing passes
  m_graph.deinit(m_context_manager);

  // 2. Add passes based on mode
  if (m_render_mode == RenderMode::RASTER)
  {
    // Raster Configuration: Sky -> Geometry -> ToneMap
    m_graph.addPass(std::make_unique<SkyPass>());
    m_graph.addPass(std::make_unique<RasterPass>(&m_descPack));
  }
  else
  {
    m_accel = AccelerationStructures::create(m_context_manager, scene.data());
    m_graph.addPass(std::make_unique<RayTracePass>(&m_descPack));
  }

  // Common: Post Processing
  auto tonePass = std::make_unique<ToneMapPass>();
  m_post = tonePass.get();  // Cache pointer for UI access
  m_graph.addPass(std::move(tonePass));

  if (m_swapchain_manager)
  {
    m_graph.addPass(
        std::make_unique<UIPass>(m_swapchain_manager->getUICallback()));
  }

  m_graph.init(m_context_manager, scene);
  m_graph.compile();
}

/**********************************************************/
void VulkanRenderer::render(IRenderContext& ctx) const
/**********************************************************/
{
  auto& vkCtx = VulkanRenderContext::get(ctx);
  vkCtx.gBuffers = m_gBuffers.get();
  vkCtx.deviceResources = &m_resources->deviceResources();
  vkCtx.pushValues.sceneInfoAddress =
      updateSceneBuffer(vkCtx.cmdBuffer, vkCtx.sceneResources->sceneInfo);
  vkCtx.bvh = m_accel.get();
  if (m_swapchain_manager)
  {
    vkCtx.swapchainImage = m_swapchain_manager->getSwapchain().getImage();
    vkCtx.swapchainImageView =
        m_swapchain_manager->getSwapchain().getImageView();
    vkCtx.screenSize = m_swapchain_manager->getWindowSize();
  }
  m_graph.execute(ctx);
}

/**********************************************************/
void VulkanRenderer::onResize(const WindowSize& size)
/**********************************************************/
{
  m_context_manager->waitForDeviceIdle();
  VkCommandBuffer cmd = m_context_manager->startSingleTimeCmd();
  NVVK_CHECK(m_gBuffers->update(cmd, {size.width, size.height}));
  m_context_manager->endSingleTimeCmd(cmd);
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
      .allocator = &m_context_manager->getAllocator(),
      .colorFormats = {VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM},
      .depthFormat =
          nvvk::findDepthFormat(m_context_manager->getPhysicalDevice()),
      .imageSampler = linearSampler,
      .descriptorPool = m_context_manager->getDescriptorPool()};

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
       .descriptorCount = 10,
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
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

/**********************************************************/
void* VulkanRenderer::getImageDescriptor(RenderOutput output) const
/**********************************************************/
{
  return static_cast<void*>(m_gBuffers->getDescriptorSet(output));
}

/**********************************************************/
void VulkanRenderer::saveImage(const std::filesystem::path& filename,
                               int quality) const
/**********************************************************/
{
  VkDevice device = m_context_manager->getDevice();
  VkPhysicalDevice physicalDevice = m_context_manager->getPhysicalDevice();
  VkImage dstImage = {};
  VkDeviceMemory dstImageMemory = {};
  VkCommandBuffer cmd = m_context_manager->startSingleTimeCmd();

  VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
  if (filename.extension() == ".hdr")
  {
    format = VK_FORMAT_R32G32B32A32_SFLOAT;
  }

  auto srcImage = m_gBuffers->getColorImage(RenderOutput::ToneMapped);
  VkExtent2D size = m_gBuffers->getSize();
  nvvk::imageToLinear(cmd, device, physicalDevice, srcImage, size, dstImage,
                      dstImageMemory, format);

  m_context_manager->endSingleTimeCmd(cmd);
  nvvk::saveImageToFile(device, dstImage, dstImageMemory, size, filename,
                        quality);

  // Clean up resources
  vkUnmapMemory(device, dstImageMemory);
  vkFreeMemory(device, dstImageMemory, nullptr);
  vkDestroyImage(device, dstImage, nullptr);
}

/**********************************************************/
IToneMapper& VulkanRenderer::postProcessor() noexcept
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
