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
#include <shaders/post/IToneMapper.hpp>

#include "backend/vulkan/VulkanRenderer.hpp"
#include "scene/gltf/io_gltf.h"

SceneManager::SceneManager(std::shared_ptr<ISceneRenderer> renderer)
{
  m_renderer = std::move(renderer);
  m_scene_resources.init(m_renderer->deviceResources());
}

void SceneManager::clear()
{
  m_scene_resources.clear();
  m_renderer->deinit();
}

void SceneManager::postInit()
{

  m_renderer->init(m_scene_resources);
}

void SceneManager::render(bool raytrace)
{
  m_scene_resources.update_scene_info(m_camera);
  shaderio::GltfSceneInfo* addr = m_renderer->update_buffers(m_camera, m_scene_resources);
  shaderio::PushConstant pushValues{
      .sceneInfoAddress = addr,
      .metallicRoughnessOverride = m_metallicRoughnessOverride,
  };
  m_renderer->render(m_camera, m_scene_resources, raytrace, pushValues);
}

// --------------------------------------------------
// Rendering / Post-processing
// --------------------------------------------------

void SceneManager::post_process()
{
  m_renderer->post_process();
}

void SceneManager::reload(bool use_raytracing)
{
  m_renderer->reload(use_raytracing);
}

core::Image SceneManager::get_image(int buffer_idx)
{
  return m_renderer->get_image(buffer_idx);
}

void SceneManager::onResize(const WindowSize& size)
{
  m_renderer->onResize(size);
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

SceneResourcesManager& SceneManager::scene_resources()
{
  return m_scene_resources;
}

// --------------------------------------------------
// Rendering parameters
// --------------------------------------------------

shaderio::TonemapperData& SceneManager::tonemapper()
{
  return m_renderer->post_processor().data();
}

glm::vec2& SceneManager::metallic_roughness()
{
  return m_metallicRoughnessOverride;
}

shaderio::GltfSceneInfo& SceneManager::scene_info()
{
  return m_scene_resources.scene_info();
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
