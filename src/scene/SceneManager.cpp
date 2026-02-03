#include "SceneManager.hpp"

#include "backend/interfaces/IRenderContext.hpp"
#include "backend/interfaces/ISceneRenderer.hpp"
#include "backend/interfaces/IToneMapper.hpp"
#include "scene/gltf/io_gltf.h"
#include "shaders/shaderio.h"

/**********************************************************/
SceneManager::SceneManager(std::shared_ptr<ISceneRenderer> renderer)
/**********************************************************/
{
  m_renderer = std::move(renderer);
  m_scene_resources.init(m_renderer->deviceResources());
}

/**********************************************************/
void SceneManager::clear()
/**********************************************************/
{
  m_scene_resources.clear();
  m_renderer->deinit();
}

/**********************************************************/
void SceneManager::postInit()
/**********************************************************/
{
  m_renderer->init(m_scene_resources);
}

/**********************************************************/
void SceneManager::render(RenderMode mode, const IRenderContext& ctx)
/**********************************************************/
{
  m_scene_resources.updateSceneInfo(m_camera);
  shaderio::PushConstant pushValues{
      .metallicRoughnessOverride = m_metallicRoughnessOverride,
  };

  IRenderContext& ctx_ref = const_cast<IRenderContext&>(ctx);
  ctx_ref.pushValues = pushValues;
  ctx_ref.sceneResources = &m_scene_resources.data();
  m_renderer->render(ctx_ref);
}

// --------------------------------------------------
// Rendering / Post-processing
// --------------------------------------------------

/**********************************************************/
void SceneManager::reload()
/**********************************************************/
{
  m_renderer->reload(m_scene_resources);
}

/**********************************************************/
void* SceneManager::getTonemapedImageDescriptor()
/**********************************************************/
{
  return m_renderer->getImageDescriptor(ISceneRenderer::ToneMapped);
}

/**********************************************************/
void SceneManager::onResize(const WindowSize& size)
/**********************************************************/
{
  m_renderer->onResize(size);
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
SceneResourcesManager& SceneManager::sceneResources()
/**********************************************************/
{
  return m_scene_resources;
}

// --------------------------------------------------
// Rendering parameters
// --------------------------------------------------

/**********************************************************/
void SceneManager::setRenderMode(RenderMode mode)
/**********************************************************/
{
  m_renderer->setRenderMode(mode, m_scene_resources);
}

/**********************************************************/
shaderio::TonemapperData& SceneManager::tonemapper()
/**********************************************************/
{
  return m_renderer->postProcessor().data();
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
