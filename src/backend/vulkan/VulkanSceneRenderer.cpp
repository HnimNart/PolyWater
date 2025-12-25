#include "VulkanSceneRenderer.hpp"

// Full headers required for implementation
#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/formats.hpp>
#include <nvvk/gbuffers.hpp>  // Full definition needed here

#include "VulkanContext.hpp"
#include "VulkanRaster.hpp"
#include "VulkanRayTracer.hpp"
#include "backend/vulkan/VulkanSceneResources.hpp"
#include "core/Camera.hpp"
#include "scene/SceneResources.hpp"
#include "shaders/post/tonemapper.hpp"

// Constructor
VulkanSceneRenderer::VulkanSceneRenderer(VulkanContext* ctx) : m_ctx(ctx)
{
  // Initialize unique_ptrs
  // Note: getShaderDirs is likely from path_utils.hpp or a similar util header
  m_compiler = ctx->slang_compiler;
  m_resources = std::make_shared<VulkanSceneResources>(ctx);

  m_raster = std::make_unique<VulkanRaster>();
  m_ray_tracer = std::make_unique<VulkanRayTracer>();
  m_post = std::make_unique<PostProcessor>();
  m_gBuffers = std::make_unique<nvvk::GBuffer>();

  // Initialization logic
  m_raster->init(m_ctx, m_compiler.get());
  m_ray_tracer->init(m_ctx, m_compiler.get(), m_raster.get());
  m_post->init(m_ctx);
}

// Destructor must be defined here where the types are complete
VulkanSceneRenderer::~VulkanSceneRenderer() = default;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void VulkanSceneRenderer::init(CpuSceneResources& scene)
{
  init_gbuffers();
  m_resources->update_descriptors(m_raster->descPack(), m_ctx);
  m_ray_tracer->createPipeline(scene);
}

void VulkanSceneRenderer::clear()
{
  m_raster->clear(m_ctx);
  m_ray_tracer->clear(m_ctx);
  m_gBuffers->deinit();
  m_resources.reset();
  m_post->clear();
}

void VulkanSceneRenderer::reload(bool use_raytracing)
{
  use_raytracing ? m_ray_tracer->createRayTracingPipeline() : m_raster->reload();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void VulkanSceneRenderer::render(VkCommandBuffer cmd, CameraPtr camera,
                                 const CpuSceneResources& scene, bool raytrace,
                                 shaderio::PushConstant& pushValues) const
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
      .allocator = m_ctx->allocator,
      .colorFormats = {VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM},
      .depthFormat = nvvk::findDepthFormat(m_ctx->physicalDevice),
      .imageSampler = linearSampler,
      .descriptorPool = m_ctx->textureDescriptorPool,
  };

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

;