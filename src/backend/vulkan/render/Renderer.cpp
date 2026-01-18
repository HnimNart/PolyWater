#include "Renderer.hpp"

// Full headers required for implementation
#include <nvvk/check_error.hpp>
#include <nvvk/commands.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/formats.hpp>
#include <nvvk/gbuffers.hpp>
#include <nvvk/helpers.hpp>

#include "Raster.hpp"
#include "RayTracer.hpp"
#include "ToneMapper.hpp"
#include "backend/interfaces/IToneMapper.hpp"
#include "backend/vulkan/core/Backend.hpp"
#include "backend/vulkan/render/SceneAssetManager.hpp"
#include "scene/SceneResources.hpp"
#include "scene/gltf/io_gltf.h"

VulkanRenderer::VulkanRenderer(VulkanBackend* backend)
{
  m_backend = backend;
  m_resources = std::make_shared<VulkanSceneAssetManager>(m_backend);

  m_gBuffers = std::make_unique<nvvk::GBuffer>();
  m_raster = std::make_unique<VulkanRaster>(m_backend);
  m_ray_tracer = std::make_unique<VulkanRayTracer>(m_backend);
  m_post = std::make_unique<VulkanToneMapper>(m_backend);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void VulkanRenderer::init(const SceneResourcesManager& scene)
{
  initGBuffers();
  createDescriptorSetLayout(m_backend->getDevice());
  m_resources->updateDescriptors(m_descPack);

  m_raster->setDescriptorPack(&m_descPack);
  m_raster->init();

  m_ray_tracer->setDescriptorPack(&m_descPack);
  m_ray_tracer->init(scene);

  m_post->init();
}

void VulkanRenderer::deinit()
{
  m_backend->waitForDeviceIdle();

  m_ray_tracer.reset();
  m_raster.reset();
  m_post.reset();

  m_descPack.deinit();
  m_gBuffers->deinit();
  m_resources->deinit();
}

void VulkanRenderer::reload(bool useRaytracing)
{
  m_backend->waitForDeviceIdle();
  useRaytracing ? m_ray_tracer->createRayTracingPipeline() : m_raster->reload();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
shaderio::GltfSceneInfo* VulkanRenderer::updateSceneBuffer(VkCommandBuffer cmd,
                                                           SceneResourcesManager& scene) const
{
  NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

  const VulkanSceneGpuData& device_resources = m_resources->deviceResources();
  shaderio::GltfSceneInfo& scene_info = scene.sceneInfo();
  scene_info.instances =
      (shaderio::GltfInstance*)
          device_resources.bInstances.address;  // Get the address of the instance buffer
  scene_info.meshes =
      (shaderio::GltfMesh*) device_resources.bMeshes.address;  // Get the address of the mesh buffer
  scene_info.materials =
      (shaderio::GltfMetallicRoughness*)
          device_resources.bMaterials.address;  // Get the address of the material buffer

  // Making sure the scene information buffer is updated before rendering
  nvvk::cmdBufferMemoryBarrier(cmd, {device_resources.bSceneInfo.buffer,
                                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                     VK_PIPELINE_STAGE_2_TRANSFER_BIT});
  vkCmdUpdateBuffer(cmd, device_resources.bSceneInfo.buffer, 0, sizeof(shaderio::GltfSceneInfo),
                    &scene.sceneInfo());
  nvvk::cmdBufferMemoryBarrier(cmd, {device_resources.bSceneInfo.buffer,
                                     VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT});
  return reinterpret_cast<shaderio::GltfSceneInfo*>(device_resources.bSceneInfo.address);
}

shaderio::GltfSceneInfo* VulkanRenderer::updateSceneBuffers(SceneResourcesManager& scene)
{
  VkCommandBuffer cmd = m_backend->getActiveCmd();
  return updateSceneBuffer(cmd, scene);
}

void VulkanRenderer::render(CameraPtr camera, const SceneResourcesManager& scene, bool raytrace,
                            const shaderio::PushConstant& pushValues) const
{
  VkCommandBuffer cmd = m_backend->getActiveCmd();
  if (raytrace)
  {
    m_ray_tracer->render(cmd, *m_gBuffers, pushValues);
  }
  else
  {
    m_raster->render(cmd, *m_gBuffers, scene.data(), m_resources->deviceResources(), camera,
                     pushValues);
  }
}

void VulkanRenderer::postProcess()
{
  VkCommandBuffer cmd = m_backend->getActiveCmd();
  NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight
  m_post->run(cmd, *m_gBuffers);
  // Barrier to make sure the image is ready for been display
  nvvk::cmdMemoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
}

void VulkanRenderer::onResize(const WindowSize& size)
{
  m_backend->waitForDeviceIdle();
  VkCommandBuffer cmd = m_backend->startSingleTimeCmd();
  NVVK_CHECK(m_gBuffers->update(cmd, {size.width, size.height}));
  m_backend->endSingleTimeCmd(cmd);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void VulkanRenderer::initGBuffers()
{
  VkSampler linearSampler{};
  NVVK_CHECK(m_resources->samplerPool().acquireSampler(linearSampler));
  NVVK_DBG_NAME(linearSampler);

  nvvk::GBufferInitInfo info{
      .allocator = &m_backend->allocator(),
      .colorFormats = {VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM},
      .depthFormat = nvvk::findDepthFormat(m_backend->getPhysicalDevice()),
      .imageSampler = linearSampler,
      .descriptorPool = m_backend->descriptorPool()};

  m_gBuffers->init(info);
}

void VulkanRenderer::createDescriptorSetLayout(VkDevice device)
{
  nvvk::DescriptorBindings bindings;
  bindings.addBinding({.binding = shaderio::BindingPoints::eTextures,
                       .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                       .descriptorCount = 10,
                       .stageFlags = VK_SHADER_STAGE_ALL},
                      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                          VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
                          VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);

  m_descPack.init(bindings, device, 1, VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                  VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT |
                      VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);

  NVVK_DBG_NAME(m_descPack.getLayout());
  NVVK_DBG_NAME(m_descPack.getPool());
  NVVK_DBG_NAME(m_descPack.getSet(0));
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

void* VulkanRenderer::getImageDescriptor(ISceneRenderer::RenderOutput output) const
{
  return static_cast<void*>(m_gBuffers->getDescriptorSet(output));
}

void VulkanRenderer::saveImage(const std::filesystem::path& filename, int quality) const
{
  VkDevice device = m_backend->getDevice();
  VkPhysicalDevice physicalDevice = m_backend->getPhysicalDevice();
  VkImage dstImage = {};
  VkDeviceMemory dstImageMemory = {};
  VkCommandBuffer cmd = m_backend->startSingleTimeCmd();

  VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
  if (filename.extension() == ".hdr")
  {
    format = VK_FORMAT_R32G32B32A32_SFLOAT;
  }

  auto srcImage = m_gBuffers->getColorImage(ISceneRenderer::RenderOutput::ToneMapped);
  VkExtent2D size = m_gBuffers->getSize();
  nvvk::imageToLinear(cmd, device, physicalDevice, srcImage, size, dstImage, dstImageMemory,
                      format);

  m_backend->endSingleTimeCmd(cmd);
  nvvk::saveImageToFile(device, dstImage, dstImageMemory, size, filename, quality);

  // Clean up resources
  vkUnmapMemory(device, dstImageMemory);
  vkFreeMemory(device, dstImageMemory, nullptr);
  vkDestroyImage(device, dstImage, nullptr);
}

IToneMapper& VulkanRenderer::postProcessor() noexcept
{
  return *m_post;
}

std::shared_ptr<IDeviceAssets> VulkanRenderer::deviceResources() noexcept
{
  return m_resources;
}
