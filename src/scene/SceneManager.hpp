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

#include "SceneResources.hpp"
#include "backend/vulkan/VulkanContext.hpp"
#include "backend/vulkan/VulkanSceneRenderer.hpp"
#include "core/Camera.hpp"

// TODO Switch out VulkanSceneRenderer with ISceneRenderer
class SceneManager
{
public:
  SceneManager(std::shared_ptr<VulkanSceneRenderer> backend)
  {
    m_backend = std::move(backend);
    m_scene_resources.init(m_backend->deviceResources());
  }

  void clear(VulkanContext* ctx)
  {
    m_scene_resources.clear();
    m_backend->clear();
  }

  void postInit() { m_backend->init(m_scene_resources); }

  void render(VkCommandBuffer cmd, bool raytrace)
  {
    // Push constant information
    shaderio::PushConstant pushValues{
        .sceneInfoAddress = (shaderio::GltfSceneInfo*) gltf_resources().bSceneInfo.address,
        .metallicRoughnessOverride = m_metallicRoughnessOverride,
    };
    m_backend->render(cmd, m_camera, m_scene_resources, raytrace, pushValues);
  }

  // --------------------------------------------------
  // Rendering / Post-processing
  // --------------------------------------------------
  void post_process(VkCommandBuffer cmd) { m_backend->post_process(cmd); }
  void reload(bool use_raytracing) { m_backend->reload(use_raytracing); }
  VkImage get_image(int buffer_idx) { return m_backend->get_image(buffer_idx); }
  void onResize(VkCommandBuffer cmd, const VkExtent2D& size) { m_backend->onResize(cmd, size); }

  // --------------------------------------------------
  // Scene / Resources
  // --------------------------------------------------
  nvsamples::GltfSceneResource& gltf_resources() { return m_scene_resources.data(); }
  const nvsamples::GltfSceneResource& gltf_resources() const { return m_scene_resources.data(); }
  CpuSceneResources& scene_resources() { return m_scene_resources; }

  // --------------------------------------------------
  // Rendering parameters
  // --------------------------------------------------
  shaderio::TonemapperData& tonemapper() { return m_backend->post_processor().data(); }
  glm::vec2& metallic_roughness() { return m_metallicRoughnessOverride; }

  // --------------------------------------------------
  // Camera
  // --------------------------------------------------
  void set_camera(CameraPtr camera) { m_camera = std::move(camera); }
  CameraPtr camera() const { return m_camera; }

private:
  CameraPtr m_camera{std::make_shared<nvutils::CameraManipulator>()};
  CpuSceneResources m_scene_resources{};
  std::shared_ptr<VulkanSceneRenderer> m_backend = nullptr;

  glm::vec2 m_metallicRoughnessOverride{
      -0.01f, -0.01f};  // Override values for metallic and roughness, used
                        // in the UI to control the material properties
};