#pragma once

#include <memory>

#include <glm/glm.hpp>
#include <nvutils/camera_manipulator.hpp>

#include "SceneResources.hpp"
#include "backend/interfaces/IRenderer.hpp"
#include "core/Camera.hpp"

namespace shaderio
{
class TonemapperData;
}

class IRenderer;
class IRenderContext;

class SceneManager
{
public:
  SceneManager() = default;
  explicit SceneManager(const std::shared_ptr<IRenderer>& renderer);
  void clear();

  // --------------------------------------------------
  // Scene / Resources
  // --------------------------------------------------
  void update();
  Scene* getScenePtr();
  Scene& gltfResources();
  const Scene& gltfResources() const;
  SceneResourcesManager& sceneResourceManager();

  // --------------------------------------------------
  // Rendering parameters
  // --------------------------------------------------
  glm::vec2& metallicRoughness();
  shaderio::SceneInfo& sceneInfo();

  // --------------------------------------------------
  // Camera
  // --------------------------------------------------
  void setCamera(CameraPtr camera);
  CameraPtr camera() const;

private:
  CameraPtr m_camera{std::make_shared<nvutils::CameraManipulator>()};
  SceneResourcesManager m_scene_resources{};

  bool has_changed = false;
};

;
