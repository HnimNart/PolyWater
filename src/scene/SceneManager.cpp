#include "SceneManager.hpp"

#include "backend/interfaces/IRenderer.hpp"
#include "scene/gltf/io_gltf.h"

/**********************************************************/
SceneManager::SceneManager(const std::shared_ptr<IRenderer>& renderer)
/**********************************************************/
{
  m_scene_resources.init(renderer->deviceResources());
}

/**********************************************************/
void SceneManager::clear()
/**********************************************************/
{
  m_scene_resources.clear();
}

/**********************************************************/
gltf::Scene* SceneManager::getScenePtr()
/**********************************************************/
{
  m_scene_resources.updateSceneInfo(m_camera);
  return &gltfResources();
}

// --------------------------------------------------
// Scene / Resources
// --------------------------------------------------

/**********************************************************/
gltf::Scene& SceneManager::gltfResources()
/**********************************************************/
{
  return m_scene_resources.data();
}

/**********************************************************/
const gltf::Scene& SceneManager::gltfResources() const
/**********************************************************/
{
  return m_scene_resources.data();
}

/**********************************************************/
SceneResourcesManager& SceneManager::sceneResourceManager()
/**********************************************************/
{
  return m_scene_resources;
}

/**********************************************************/
glm::vec2& SceneManager::metallicRoughness()
/**********************************************************/
{
  return m_metallicRoughnessOverride;
}

/**********************************************************/
shaderio::GltfSceneInfo& SceneManager::sceneInfo()
/**********************************************************/
{
  return m_scene_resources.sceneInfo();
}

// --------------------------------------------------
// Camera
// --------------------------------------------------

/**********************************************************/
void SceneManager::setCamera(CameraPtr camera)
/**********************************************************/
{
  m_camera = std::move(camera);
}

/**********************************************************/
CameraPtr SceneManager::camera() const
/**********************************************************/
{
  return m_camera;
}
