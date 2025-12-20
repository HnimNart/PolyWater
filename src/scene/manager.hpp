#pragma once

#include <shaders/shaderio.h>

#include <memory>
#include <nvshaders_host/sky.hpp>
#include <nvshaders_host/tonemapper.hpp>
#include <nvslang/slang.hpp>
#include <nvutils/camera_manipulator.hpp>
#include <nvvk/acceleration_structures.hpp>  // Acceleration structure management
#include <nvvk/descriptors.hpp>
#include <nvvk/gbuffers.hpp>  // GBuffer management
#include <nvvk/graphics_pipeline.hpp>
#include <nvvk/sampler_pool.hpp>
#include <nvvk/sbt_generator.hpp>

#include "_autogen/tonemapper.slang.h"  //   "    "
#include "nvvk/formats.hpp"
#include "scene/vulkan_raster.hpp"
#include "scene/vulkan_raytrace.hpp"
#include "scene_context.hpp"
#include "scene_resources.hpp"
#include "shaders/compiler/slang.hpp"

class SceneManager
{
public:
  SceneManager(VulkanContext* ctx)
  {
    m_ctx = ctx;
    m_scene_resources.init(m_ctx);

    // Create the G-Buffers
    nvvk::GBufferInitInfo gBufferInit{
        .allocator = m_ctx->allocator,
        .colorFormats = {VK_FORMAT_R32G32B32A32_SFLOAT,
                         VK_FORMAT_R8G8B8A8_UNORM},  // Render target, tonemapped
        .depthFormat = nvvk::findDepthFormat(m_ctx->physicalDevice),
        .imageSampler = m_scene_resources.sampler(),
        .descriptorPool = m_ctx->textureDescriptorPool,
    };
    m_gBuffers.init(gBufferInit);

    m_raster.init(m_ctx, &m_compiler);
    m_ray_tracer.init(m_ctx, &m_compiler, &m_raster);
  }

  void clear()
  {
    m_scene_resources.clear(m_ctx);
    m_raster.clear(m_ctx);
    m_ray_tracer.clear(m_ctx);
    m_tonemapper.deinit();
    m_gBuffers.deinit();
  }

  void postInit()
  {
    m_scene_resources.updateTextures(m_raster.descPack(), m_ctx);
    m_ray_tracer.createPipeline(m_ctx, m_scene_resources);
    // Initialize the tonemapper also with proe-compiled shader
    m_tonemapper.init(m_ctx->allocator, std::span(tonemapper_slang));
  }

  void render(VkCommandBuffer cmd, bool raytrace)
  {
    // Push constant information
    shaderio::PushConstant pushValues{
        .sceneInfoAddress = (shaderio::GltfSceneInfo*) gltf_resources().bSceneInfo.address,
        .metallicRoughnessOverride = m_metallicRoughnessOverride,
    };

    if (raytrace)
    {
      m_ray_tracer.render(cmd, m_gBuffers, m_ctx, pushValues);
    }
    else
    {
      m_raster.render(cmd, m_gBuffers, m_scene_resources, m_cameraManip, pushValues);
    }
  }

  void post_process(VkCommandBuffer cmd)
  {
    // Default post-processing: tonemapping
    m_tonemapper.runCompute(cmd, m_gBuffers.getSize(), m_tonemapperData,
                            m_gBuffers.getDescriptorImageInfo(0),
                            m_gBuffers.getDescriptorImageInfo(1));
  }

  void reload(bool use_raytracing)
  {
    if (use_raytracing)
    {
      m_ray_tracer.createRayTracingPipeline(m_ctx);
    }
    else
    {
      m_raster.reload(m_ctx->device);
    }
  }

  VkImage get_image(int buffer_idx) { return m_gBuffers.getColorImage(buffer_idx); }

  void onResize(VkCommandBuffer cmd, const VkExtent2D& size)
  {
    NVVK_CHECK(m_gBuffers.update(cmd, size));
  }

  std::shared_ptr<nvutils::CameraManipulator> camera() const { return m_cameraManip; }
  nvsamples::GltfSceneResource& gltf_resources() { return m_scene_resources.data(); }
  const nvsamples::GltfSceneResource& gltf_resources() const { return m_scene_resources.data(); }
  SceneResources& scene_resources() { return m_scene_resources; }
  const nvvk::GBuffer& gbuffers() const { return m_gBuffers; }

  shaderio::TonemapperData& tonemapper() { return m_tonemapperData; }
  glm::vec2& metallic_roughness() { return m_metallicRoughnessOverride; }

  void set_camera(std::shared_ptr<nvutils::CameraManipulator> camera)
  {
    m_cameraManip = std::move(camera);
  }

private:
  VulkanContext* m_ctx = nullptr;

  nvvk::GBuffer m_gBuffers{};  // The G-Buffer
  // Camera manipulator
  std::shared_ptr<nvutils::CameraManipulator> m_cameraManip{
      std::make_shared<nvutils::CameraManipulator>()};

  VulkanRaster m_raster;
  VulkanRayTracer m_ray_tracer;
  SceneResources m_scene_resources{};

  nvshaders::Tonemapper m_tonemapper{};  // Tonemapper for post-processing effects
  shaderio::TonemapperData
      m_tonemapperData{};  // Tonemapper data used to pass parameters to the tonemapper shader
  glm::vec2 m_metallicRoughnessOverride{
      -0.01f, -0.01f};  // Override values for metallic and roughness, used
                        // in the UI to control the material properties

  SlangShaderCompiler m_compiler{nvsamples::getShaderDirs()};
};