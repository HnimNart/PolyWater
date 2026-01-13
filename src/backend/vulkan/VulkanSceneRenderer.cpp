#include "VulkanSceneRenderer.hpp"

// Full headers required for implementation
#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/formats.hpp>
#include <nvvk/gbuffers.hpp>

#include "VulkanBackend.hpp"
#include "VulkanContext.hpp"
#include "VulkanRaster.hpp"
#include "VulkanRayTracer.hpp"
#include "backend/vulkan/VulkanSceneResources.hpp"
#include "core/Camera.hpp"
#include "scene/SceneResources.hpp"
#include "shaders/post/tonemapper.hpp"

// Constructor
VulkanSceneRenderer::VulkanSceneRenderer(core::VulkanBackend* backend)
{

  // Initialize unique_ptrs
  m_backend = backend;
  m_compiler = m_backend->get_slang_compiler();
  m_resources = std::make_shared<VulkanSceneResources>(m_backend);

  m_raster = std::make_unique<VulkanRaster>();
  m_ray_tracer = std::make_unique<VulkanRayTracer>();
  m_post = std::make_unique<PostProcessor>();
  m_gBuffers = std::make_unique<nvvk::GBuffer>();

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
  m_backend->deinit();
}

void VulkanSceneRenderer::reload(bool use_raytracing)
{
  use_raytracing ? m_ray_tracer->createRayTracingPipeline() : m_raster->reload();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void VulkanSceneRenderer::render(VkCommandBuffer cmd, CameraPtr camera, const SceneResources& scene,
                                 bool raytrace, shaderio::PushConstant& pushValues) const
{
  if (raytrace)
  {
    m_ray_tracer->render(cmd, *m_gBuffers, pushValues);
    return;
  }

  m_raster->render(cmd, *m_gBuffers, scene, camera, pushValues);
}

void VulkanSceneRenderer::post_process(VkCommandBuffer cmd)
{
  m_post->run(cmd, *m_gBuffers);
}

void VulkanSceneRenderer::onResize(VkCommandBuffer cmd, const VkExtent2D& size)
{
  NVVK_CHECK(m_gBuffers->update(cmd, size));
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
      .depthFormat = nvvk::findDepthFormat(m_backend->get_context().getPhysicalDevice()),
      .imageSampler = linearSampler,
      .descriptorPool = m_backend->descriptorPool()};

  m_gBuffers->init(info);
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

VkImage VulkanSceneRenderer::get_image(uint32_t index) const
{
  return m_gBuffers->getColorImage(index);
}

VulkanRaster& VulkanSceneRenderer::raster() noexcept
{
  return *m_raster;
}

const VulkanRaster& VulkanSceneRenderer::raster() const noexcept
{
  return *m_raster;
}

VulkanRayTracer& VulkanSceneRenderer::ray_tracer() noexcept
{
  return *m_ray_tracer;
}

const VulkanRayTracer& VulkanSceneRenderer::ray_tracer() const noexcept
{
  return *m_ray_tracer;
}

PostProcessor& VulkanSceneRenderer::post_processor() noexcept
{
  return *m_post;
}

const PostProcessor& VulkanSceneRenderer::post_processor() const noexcept
{
  return *m_post;
}

const nvvk::GBuffer& VulkanSceneRenderer::gbuffers() const noexcept
{
  return *m_gBuffers;
}

std::shared_ptr<VulkanSceneResources> VulkanSceneRenderer::deviceResources() noexcept
{
  return m_resources;
}
