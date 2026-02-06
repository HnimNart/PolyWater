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
  gltf::Scene* getScenePtr();
  gltf::Scene& gltfResources();
  const gltf::Scene& gltfResources() const;
  SceneResourcesManager& sceneResourceManager();

  // --------------------------------------------------
  // Rendering parameters
  // --------------------------------------------------
  glm::vec2& metallicRoughness();
  shaderio::GltfSceneInfo& sceneInfo();

  // --------------------------------------------------
  // Camera
  // --------------------------------------------------
  void setCamera(CameraPtr camera);
  CameraPtr camera() const;

private:
  CameraPtr m_camera{std::make_shared<nvutils::CameraManipulator>()};
  SceneResourcesManager m_scene_resources{};
  glm::vec2 m_metallicRoughnessOverride{-0.01f, -0.01f};

  bool has_changed = false;
};

;
