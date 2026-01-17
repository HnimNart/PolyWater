#include "VulkanSceneRenderer.hpp"

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
#include "backend/vulkan/VulkanSceneResources.hpp"
#include "core/Camera.hpp"
#include "core/Image.hpp"
#include "scene/SceneResources.hpp"
#include "shaders/post/IToneMapper.hpp"

// Constructor
VulkanSceneRenderer::VulkanSceneRenderer(core::VulkanBackend* backend)
{

  // Initialize unique_ptrs
  m_backend = backend;
  m_compiler = m_backend->get_slang_compiler();
  m_resources = std::make_shared<VulkanSceneResources>(m_backend);

  m_gBuffers = std::make_unique<nvvk::GBuffer>();
  m_raster = std::make_unique<VulkanRaster>();
  m_ray_tracer = std::make_unique<VulkanRayTracer>();
  m_post = std::make_unique<VulkanPostProcessor>();

  // Initialization logic
  m_raster->init(m_backend);
  m_ray_tracer->init(m_backend, m_raster.get());
  m_post->init(m_backend);
}

// Destructor must be defined here where the types are complete
VulkanSceneRenderer::~VulkanSceneRenderer() = default;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void VulkanSceneRenderer::init(SceneResources& scene)
{
  printf("Updated\n");
  init_gbuffers();
  m_resources->update_descriptors(m_raster->descPack());
  m_ray_tracer->createPipeline(scene);
}

void VulkanSceneRenderer::deinit()
{
  m_raster->deinit();
  m_ray_tracer->deinit();
  m_gBuffers->deinit();
  m_resources->deinit();
  m_post->deinit();
}

void VulkanSceneRenderer::reload(bool use_raytracing)
{
  use_raytracing ? m_ray_tracer->createRayTracingPipeline() : m_raster->reload();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void VulkanSceneRenderer::updateSceneBuffer(VkCommandBuffer cmd, CameraPtr camera,
                                            SceneResources& scene) const
{
  NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight
  const glm::mat4& viewMatrix = camera->getViewMatrix();
  const glm::mat4& projMatrix = camera->getPerspectiveMatrix();

  scene.data().sceneInfo.viewProjMatrix =
      projMatrix * viewMatrix;  // Combine the view and projection matrices
  scene.data().sceneInfo.projInvMatrix = glm::inverse(projMatrix);  // Inverse projection matrix
  scene.data().sceneInfo.viewInvMatrix = glm::inverse(viewMatrix);  // Inverse view matrix
  scene.data().sceneInfo.cameraPosition = camera->getEye();         // Get the camera position
  scene.data().sceneInfo.instances =
      (shaderio::GltfInstance*) scene.data()
          .bInstances.address;  // Get the address of the instance buffer
  scene.data().sceneInfo.meshes =
      (shaderio::GltfMesh*) scene.data().bMeshes.address;  // Get the address of the mesh buffer
  scene.data().sceneInfo.materials =
      (shaderio::GltfMetallicRoughness*) scene.data()
          .bMaterials.address;  // Get the address of the material buffer

  // Making sure the scene information buffer is updated before rendering
  // Wait that the fragment shader is done reading the previous scene information and wait for the
  // transfer to complete
  nvvk::cmdBufferMemoryBarrier(cmd, {scene.data().bSceneInfo.buffer,
                                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                     VK_PIPELINE_STAGE_2_TRANSFER_BIT});
  vkCmdUpdateBuffer(cmd, scene.data().bSceneInfo.buffer, 0, sizeof(shaderio::GltfSceneInfo),
                    &scene.data().sceneInfo);
  nvvk::cmdBufferMemoryBarrier(cmd,
                               {scene.data().bSceneInfo.buffer, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT});
}

void VulkanSceneRenderer::render(CameraPtr camera, SceneResources& scene, bool raytrace,
                                 shaderio::PushConstant& pushValues) const
{
  VkCommandBuffer cmd = m_backend->get_active_cmd();
  updateSceneBuffer(cmd, camera, scene);
  if (raytrace)
  {
    m_ray_tracer->render(cmd, *m_gBuffers, pushValues);
    return;
  }
  m_raster->render(cmd, *m_gBuffers, scene, camera, pushValues);
}

void VulkanSceneRenderer::post_process()
{
  VkCommandBuffer cmd = m_backend->get_active_cmd();
  NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight
  m_post->run(cmd, *m_gBuffers);
  // Barrier to make sure the image is ready for been display
  nvvk::cmdMemoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
}

void VulkanSceneRenderer::onResize(const WindowSize& size)
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

void VulkanSceneRenderer::init_gbuffers()
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

core::Image VulkanSceneRenderer::get_image(uint32_t index) const
{
  core::Image image;
  image.descriptor = static_cast<void*>(m_gBuffers->getDescriptorSet(index));
  // image.native_handle = static_cast<void*>(&m_gBuffers->getColorImage(index));
  return image;
}

IPostProcessor& VulkanSceneRenderer::post_processor() noexcept
{
  return *m_post;
}

std::shared_ptr<VulkanSceneResources> VulkanSceneRenderer::deviceResources() noexcept
{
  return m_resources;
}
