#include "VulkanRenderer.hpp"

// Full headers required for implementation
#include <nvvk/check_error.hpp>
#include <nvvk/commands.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/formats.hpp>
#include <nvvk/gbuffers.hpp>

#include "VulkanBackend.hpp"
#include "VulkanPostToneMapper.hpp"
#include "VulkanRaster.hpp"
#include "VulkanRayTracer.hpp"
#include "backend/vulkan/VulkanRenderResources.hpp"
#include "core/Camera.hpp"
#include "core/Image.hpp"
#include "scene/SceneResources.hpp"
#include "scene/gltf/io_gltf.h"
#include "shaders/post/IToneMapper.hpp"

VulkanRenderer::VulkanRenderer(core::VulkanBackend* backend)
{
  m_backend = backend;
  m_resources = std::make_shared<VulkanRenderResources>(m_backend);

  m_gBuffers = std::make_unique<nvvk::GBuffer>();
  m_raster = std::make_unique<VulkanRaster>(m_backend);
  m_ray_tracer = std::make_unique<VulkanRayTracer>(m_backend, m_raster.get());
  m_post = std::make_unique<VulkanPostProcessor>(m_backend);
}

VulkanRenderer::~VulkanRenderer() = default;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void VulkanRenderer::init(const SceneResourcesManager& scene)
{
  initGBuffers();
  m_resources->updateDescriptors(m_raster->descPack());
  m_ray_tracer->createPipeline(scene);
}

void VulkanRenderer::deinit()
{
  m_backend->waitForDeviceIdle();

  m_raster.reset();
  m_ray_tracer.reset();
  m_post.reset();

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

  const GltfDeviceSceneResources& device_resources = m_resources->deviceResources();
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
                            shaderio::PushConstant& pushValues) const
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

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

core::Image VulkanRenderer::getImage(uint32_t index) const
{
  // TODO fix this
  core::Image image;
  image.descriptor = static_cast<void*>(m_gBuffers->getDescriptorSet(index));
  // image.native_handle = static_cast<void*>(&m_gBuffers->getColorImage(index));
  return image;
}

IPostProcessor& VulkanRenderer::postProcessor() noexcept
{
  return *m_post;
}

std::shared_ptr<IDeviceResources> VulkanRenderer::deviceResources() noexcept
{
  return m_resources;
}
