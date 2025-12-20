#pragma once

#include <nvvk/formats.hpp>

#include "context.hpp"
#include "core/camera.hpp"
#include "shaders/post/tonemapper.hpp"
#include "src/backend/backend.hpp"
#include "vulkan_raster.hpp"
#include "vulkan_raytrace.hpp"

class VulkanBackend final : public IBackend
{
public:
  explicit VulkanBackend(VulkanContext* ctx) :
      m_ctx(ctx), m_compiler(nvsamples::getShaderDirs()),
      m_resources(std::make_shared<VulkanSceneResources>(ctx))
  {
    m_raster.init(m_ctx, &m_compiler);
    m_ray_tracer.init(m_ctx, &m_compiler, &m_raster);
    m_post.init(m_ctx);
  }

  VulkanBackend(const VulkanBackend&) = delete;
  VulkanBackend& operator=(const VulkanBackend&) = delete;

  // ---------------------------------------------------------------------------
  // Lifecycle
  // ---------------------------------------------------------------------------

  void init(CpuSceneResources& scene) override
  {
    init_gbuffers();
    m_resources->update_descriptors(m_raster.descPack(), m_ctx);
    m_ray_tracer.createPipeline(scene);
  }

  void clear() override
  {
    m_raster.clear(m_ctx);
    m_ray_tracer.clear(m_ctx);
    m_gBuffers.deinit();
    m_resources.reset();
    m_post.clear();
  }

  void reload(bool use_raytracing) override
  {
    use_raytracing ? m_ray_tracer.createRayTracingPipeline() : m_raster.reload();
  }

  // ---------------------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------------------
  void render(VkCommandBuffer cmd, CameraPtr camera, const CpuSceneResources& scene, bool raytrace,
              shaderio::PushConstant& pushValues) const override
  {
    if (raytrace)
    {
      m_ray_tracer.render(cmd, m_gBuffers, pushValues);
      return;
    }

    m_raster.render(cmd, m_gBuffers, scene, camera, pushValues);
  }

  void post_process(VkCommandBuffer cmd) { m_post.run(cmd, m_gBuffers); }

  void onResize(VkCommandBuffer cmd, const VkExtent2D& size)
  {
    NVVK_CHECK(m_gBuffers.update(cmd, size));
  }

  // ---------------------------------------------------------------------------
  // Accessors
  // ---------------------------------------------------------------------------

  VkImage get_image(uint32_t index) const { return m_gBuffers.getColorImage(index); }

  VulkanRaster& raster() noexcept { return m_raster; }
  const VulkanRaster& raster() const noexcept { return m_raster; }

  VulkanRayTracer& ray_tracer() noexcept { return m_ray_tracer; }
  const VulkanRayTracer& ray_tracer() const noexcept { return m_ray_tracer; }

  PostProcessor& post_processor() noexcept { return m_post; }
  const PostProcessor& post_processor() const noexcept { return m_post; }

  const nvvk::GBuffer& gbuffers() const noexcept { return m_gBuffers; }
  std::shared_ptr<VulkanSceneResources> resources() noexcept { return m_resources; }

private:
  // ---------------------------------------------------------------------------
  // Helpers
  // ---------------------------------------------------------------------------

  void init_gbuffers()
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

    m_gBuffers.init(info);
  }

private:
  VulkanContext* m_ctx = nullptr;

  // Frame resources
  nvvk::GBuffer m_gBuffers{};

  // Render paths
  VulkanRaster m_raster;
  VulkanRayTracer m_ray_tracer;
  PostProcessor m_post;

  // Shader compiler
  SlangShaderCompiler m_compiler;

  // Scene GPU resources
  std::shared_ptr<VulkanSceneResources> m_resources;
};

;