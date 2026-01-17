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

#include "Shared.hpp"
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
  shaderio::GltfSceneInfo* addr = m_renderer->updateSceneBuffers(m_scene_resources);
  shaderio::PushConstant pushValues{
      .sceneInfoAddress = addr,
      .metallicRoughnessOverride = m_metallicRoughnessOverride,
  };
  m_renderer->render(m_camera, m_scene_resources, raytrace, pushValues);
}

// --------------------------------------------------
// Rendering / Post-processing
// --------------------------------------------------

void SceneManager::postProcess()
{
  m_renderer->postProcess();
}

void SceneManager::reload(bool useRaytracing)
{
  m_renderer->reload(useRaytracing);
}

core::Image SceneManager::getImage(int bufferIdx)
{
  return m_renderer->getImage(bufferIdx);
}

core::Image SceneManager::getTonemapedImage()
{
  return m_renderer->getImage(eImgTonemapped);
}

void SceneManager::onResize(const WindowSize& size)
{
  m_renderer->onResize(size);
}

// --------------------------------------------------
// Scene / Resources
// --------------------------------------------------

gltf::Scene& SceneManager::gltfResources()
{
  return m_scene_resources.data();
}

const gltf::Scene& SceneManager::gltfResources() const
{
  return m_scene_resources.data();
}

SceneResourcesManager& SceneManager::sceneResources()
{
  return m_scene_resources;
}

// --------------------------------------------------
// Rendering parameters
// --------------------------------------------------

shaderio::TonemapperData& SceneManager::tonemapper()
{
  return m_renderer->postProcessor().data();
}

glm::vec2& SceneManager::metallicRoughness()
{
  return m_metallicRoughnessOverride;
}

shaderio::GltfSceneInfo& SceneManager::sceneInfo()
{
  return m_scene_resources.scene_info();
}

// --------------------------------------------------
// Camera
// --------------------------------------------------

void SceneManager::setCamera(CameraPtr camera)
{
  m_camera = std::move(camera);
}

CameraPtr SceneManager::camera() const
{
  return m_camera;
}
