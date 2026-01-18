#include "SceneManager.hpp"

#include "backend/interfaces/ISceneRenderer.hpp"
#include "backend/interfaces/IToneMapper.hpp"
#include "scene/gltf/io_gltf.h"
#include "shaders/shaderio.h"

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
  m_scene_resources.updateSceneInfo(m_camera);
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

void* SceneManager::getTonemapedImageDescriptor()
{
  return m_renderer->getImageDescriptor(ISceneRenderer::ToneMapped);
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
  return m_scene_resources.sceneInfo();
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
