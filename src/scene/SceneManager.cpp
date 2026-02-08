#include "SceneManager.hpp"

#include "backend/interfaces/IRenderer.hpp"

/**********************************************************/
SceneManager::SceneManager(const std::shared_ptr<IRenderer> &renderer)
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
void SceneManager::update()
/**********************************************************/
{
  m_scene_resources.update(m_camera);
}

/**********************************************************/
Scene *SceneManager::getScenePtr()
/**********************************************************/
{
  return &gltfResources();
}

// --------------------------------------------------
// Scene / Resources
// --------------------------------------------------

/**********************************************************/
Scene &SceneManager::gltfResources()
/**********************************************************/
{
  return m_scene_resources.data();
}

/**********************************************************/
const Scene &SceneManager::gltfResources() const
/**********************************************************/
{
  return m_scene_resources.data();
}

/**********************************************************/
SceneResourcesManager &SceneManager::sceneResourceManager()
/**********************************************************/
{
  return m_scene_resources;
}

/**********************************************************/
const SceneResourcesManager &SceneManager::sceneResourceManager() const
/**********************************************************/
{
  return m_scene_resources;
}

/**********************************************************/
shaderio::SceneInfo &SceneManager::sceneInfo()
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
