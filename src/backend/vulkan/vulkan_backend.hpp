#pragma once

#include <nvvk/formats.hpp>

#include "context.hpp"
#include "core/camera.hpp"
#include "shaders/post/tonemapper.hpp"
#include "src/backend/backend.hpp"
#include "vulkan_raster.hpp"
#include "vulkan_raytrace.hpp"

class VulkanBackend : IBackend
{
public:
  VulkanBackend(VulkanContext* ctx)
  {
    m_ctx = ctx;
    m_raster.init(m_ctx, &m_compiler);
    m_ray_tracer.init(m_ctx, &m_compiler, &m_raster);
    m_post.init(m_ctx);
  }

  void init(SceneResources& scene) override
  {
    // Create the G-Buffers
    VkSampler linearSampler{};
    NVVK_CHECK(scene.sampler_pool().acquireSampler(linearSampler));
    NVVK_DBG_NAME(linearSampler);
    nvvk::GBufferInitInfo gBufferInit{
        .allocator = m_ctx->allocator,
        .colorFormats = {VK_FORMAT_R32G32B32A32_SFLOAT,
                         VK_FORMAT_R8G8B8A8_UNORM},  // Render target, tonemapped
        .depthFormat = nvvk::findDepthFormat(m_ctx->physicalDevice),
        .imageSampler = linearSampler,
        .descriptorPool = m_ctx->textureDescriptorPool,
    };
    m_gBuffers.init(gBufferInit);

    scene.updateTextures(raster().descPack(), m_ctx);
    ray_tracer().createPipeline(m_ctx, scene);
  }

  void clear() override
  {
    m_raster.clear(m_ctx);
    m_ray_tracer.clear(m_ctx);
    m_gBuffers.deinit();
  }

  void render(VkCommandBuffer cmd, CameraPtr camera, const SceneResources& scene, bool raytrace,
              shaderio::PushConstant& pushValues) const override

  {
    if (raytrace)
    {
      ray_tracer().render(cmd, m_gBuffers, m_ctx, pushValues);
    }
    else
    {
      raster().render(cmd, m_gBuffers, scene, camera, pushValues);
    }
  }

  void reload(bool use_raytracing) override
  {
    if (use_raytracing)
    {
      ray_tracer().createRayTracingPipeline(m_ctx);
    }
    else
    {
      raster().reload(m_ctx->device);
    }
  }

  void post_process(VkCommandBuffer cmd) { post_processor().run(cmd, m_gBuffers); }
  VkImage get_image(int buffer_idx) { return m_gBuffers.getColorImage(buffer_idx); }
  void onResize(VkCommandBuffer cmd, const VkExtent2D& size)
  {
    NVVK_CHECK(m_gBuffers.update(cmd, size));
  }

  VulkanRaster& raster() noexcept { return m_raster; }
  VulkanRaster const& raster() const noexcept { return m_raster; }

  VulkanRayTracer& ray_tracer() noexcept { return m_ray_tracer; }
  VulkanRayTracer const& ray_tracer() const noexcept { return m_ray_tracer; }

  PostProcessor& post_processor() noexcept { return m_post; }
  PostProcessor const& post_processor() const noexcept { return m_post; }

  const nvvk::GBuffer& gbuffers() const { return m_gBuffers; }

private:
  VulkanContext* m_ctx;
  nvvk::GBuffer m_gBuffers{};  // The G-Buffer
  VulkanRaster m_raster;
  VulkanRayTracer m_ray_tracer;
  PostProcessor m_post;

  SlangShaderCompiler m_compiler{nvsamples::getShaderDirs()};
};