#include "SceneManager.hpp"

// Implementation Headers
#include <nvshaders_host/sky.hpp>
#include <nvshaders_host/tonemapper.hpp>
#include <nvslang/slang.hpp>
#include <nvvk/acceleration_structures.hpp>
#include <nvvk/descriptors.hpp>
#include <nvvk/gbuffers.hpp>
#include <nvvk/graphics_pipeline.hpp>
#include <nvvk/sampler_pool.hpp>
#include <nvvk/sbt_generator.hpp>
#include <shaders/post/tonemapper.hpp>

#include "backend/vulkan/VulkanContext.hpp"
#include "backend/vulkan/VulkanSceneRenderer.hpp"

SceneManager::SceneManager(std::shared_ptr<VulkanSceneRenderer> backend)
{
  m_backend = std::move(backend);
  m_scene_resources.init(m_backend->deviceResources());
}

void SceneManager::clear()
{
  m_scene_resources.clear();
  m_backend->clear();
}

void SceneManager::postInit()
{
  m_backend->init(m_scene_resources);
}

void SceneManager::render(VkCommandBuffer cmd, bool raytrace)
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

void SceneManager::post_process(VkCommandBuffer cmd)
{
  m_backend->post_process(cmd);
}

void SceneManager::reload(bool use_raytracing)
{
  m_backend->reload(use_raytracing);
}

VkImage SceneManager::get_image(int buffer_idx)
{
  return m_backend->get_image(buffer_idx);
}

void SceneManager::onResize(VkCommandBuffer cmd, const VkExtent2D& size)
{
  m_backend->onResize(cmd, size);
}

// --------------------------------------------------
// Scene / Resources
// --------------------------------------------------

nvsamples::GltfSceneResource& SceneManager::gltf_resources()
{
  return m_scene_resources.data();
}

const nvsamples::GltfSceneResource& SceneManager::gltf_resources() const
{
  return m_scene_resources.data();
}

CpuSceneResources& SceneManager::scene_resources()
{
  return m_scene_resources;
}

// --------------------------------------------------
// Rendering parameters
// --------------------------------------------------

shaderio::TonemapperData& SceneManager::tonemapper()
{
  return m_backend->post_processor().data();
}

glm::vec2& SceneManager::metallic_roughness()
{
  return m_metallicRoughnessOverride;
}

// --------------------------------------------------
// Camera
// --------------------------------------------------

void SceneManager::set_camera(CameraPtr camera)
{
  m_camera = std::move(camera);
}

CameraPtr SceneManager::camera() const
{
  return m_camera;
}