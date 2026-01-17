#include "VulkanRenderer.hpp"

#include <iostream>

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
#include "scene/gltf/gltf_utils.hpp"
#include "scene/gltf/io_gltf.h"
#include "shaders/post/IToneMapper.hpp"

VulkanRenderer::VulkanRenderer(core::VulkanBackend* backend)
{
  // Initialize unique_ptrs
  m_backend = backend;
  m_compiler = m_backend->get_slang_compiler();
  m_resources = std::make_shared<VulkanRenderResources>(m_backend);

  m_gBuffers = std::make_unique<nvvk::GBuffer>();
  m_raster = std::make_unique<VulkanRaster>();
  m_ray_tracer = std::make_unique<VulkanRayTracer>();
  m_post = std::make_unique<VulkanPostProcessor>();

  // Initialization logic
  m_raster->init(m_backend);
  m_ray_tracer->init(m_backend, m_raster.get());
  m_post->init(m_backend);
}

VulkanRenderer::~VulkanRenderer() = default;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void VulkanRenderer::init(const SceneResourcesManager& scene)
{
  init_gbuffers();
  m_resources->update_descriptors(m_raster->descPack());
  m_ray_tracer->createPipeline(scene);
}

void VulkanRenderer::deinit()
{
  VkDevice device = m_backend->getDevice();
  assert(device != VK_NULL_HANDLE);
  NVVK_CHECK(vkDeviceWaitIdle(device));

  m_raster->deinit();
  m_ray_tracer->deinit();
  m_gBuffers->deinit();
  m_post->deinit();
  m_resources->deinit();
}

void VulkanRenderer::reload(bool use_raytracing)
{
  use_raytracing ? m_ray_tracer->createRayTracingPipeline() : m_raster->reload();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
shaderio::GltfSceneInfo* VulkanRenderer::updateSceneBuffer(VkCommandBuffer cmd, CameraPtr camera,
                                                           SceneResourcesManager& scene) const
{
  NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

  const nvsamples::GltfDeviceSceneResources& device_resources = m_resources->device_resources();
  scene.scene_info().instances =
      (shaderio::GltfInstance*)
          device_resources.bInstances.address;  // Get the address of the instance buffer
  scene.scene_info().meshes =
      (shaderio::GltfMesh*) device_resources.bMeshes.address;  // Get the address of the mesh buffer
  scene.scene_info().materials =
      (shaderio::GltfMetallicRoughness*)
          device_resources.bMaterials.address;  // Get the address of the material buffer

  // Making sure the scene information buffer is updated before rendering
  nvvk::cmdBufferMemoryBarrier(cmd, {device_resources.bSceneInfo.buffer,
                                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                     VK_PIPELINE_STAGE_2_TRANSFER_BIT});
  vkCmdUpdateBuffer(cmd, device_resources.bSceneInfo.buffer, 0, sizeof(shaderio::GltfSceneInfo),
                    &scene.scene_info());
  nvvk::cmdBufferMemoryBarrier(cmd, {device_resources.bSceneInfo.buffer,
                                     VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT});
  return reinterpret_cast<shaderio::GltfSceneInfo*>(device_resources.bSceneInfo.address);
}

shaderio::GltfSceneInfo* VulkanRenderer::update_buffers(CameraPtr camera,
                                                        SceneResourcesManager& scene)
{
  VkCommandBuffer cmd = m_backend->get_active_cmd();
  return updateSceneBuffer(cmd, camera, scene);
}

void VulkanRenderer::render(CameraPtr camera, const SceneResourcesManager& scene, bool raytrace,
                            shaderio::PushConstant& pushValues) const
{
  VkCommandBuffer cmd = m_backend->get_active_cmd();
  if (raytrace)
  {
    m_ray_tracer->render(cmd, *m_gBuffers, pushValues);
  }
  else
  {
    m_raster->render(cmd, *m_gBuffers, scene.data(), m_resources->device_resources(), camera,
                     pushValues);
  }
}

void VulkanRenderer::post_process()
{
  VkCommandBuffer cmd = m_backend->get_active_cmd();
  NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight
  m_post->run(cmd, *m_gBuffers);
  // Barrier to make sure the image is ready for been display
  nvvk::cmdMemoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
}

void VulkanRenderer::onResize(const WindowSize& size)
{

  NVVK_CHECK(vkQueueWaitIdle(m_backend->getQueueInfo(0).queue));
  VkCommandBuffer cmd;
  NVVK_CHECK(
      nvvk::beginSingleTimeCommands(cmd, m_backend->getDevice(), m_backend->transientCmdPool()));
  NVVK_CHECK(m_gBuffers->update(cmd, {size.width, size.height}));
  NVVK_CHECK(nvvk::endSingleTimeCommands(cmd, m_backend->getDevice(), m_backend->transientCmdPool(),
                                         m_backend->getQueueInfo(0).queue));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void VulkanRenderer::init_gbuffers()
{
  VkSampler linearSampler{};
  NVVK_CHECK(m_resources->sampler_pool().acquireSampler(linearSampler));
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

core::Image VulkanRenderer::get_image(uint32_t index) const
{
  // TODO fix this
  core::Image image;
  image.descriptor = static_cast<void*>(m_gBuffers->getDescriptorSet(index));
  // image.native_handle = static_cast<void*>(&m_gBuffers->getColorImage(index));
  return image;
}

IPostProcessor& VulkanRenderer::post_processor() noexcept
{
  return *m_post;
}

std::shared_ptr<IDeviceResources> VulkanRenderer::deviceResources() noexcept
{
  return m_resources;
}
