#pragma once

#include <memory>

#include <glm/glm.hpp>
#include <nvutils/camera_manipulator.hpp>

#include "SceneResources.hpp"
#include "backend/interfaces/ISceneRenderer.hpp"
#include "core/Camera.hpp"

namespace shaderio
{
class TonemapperData;
}

class IRenderContext;

class SceneManager
{
public:
  SceneManager() = default;
  explicit SceneManager(std::shared_ptr<ISceneRenderer> renderer);

  void clear();
  void postInit();
  void render(RenderMode mode, const IRenderContext& ctx);

  // --------------------------------------------------
  // Rendering / Post-processing
  // --------------------------------------------------
  void postProcess(const IRenderContext& ctx);
  void reload();
  void* getTonemapedImageDescriptor();
  void onResize(const WindowSize& size);

  // --------------------------------------------------
  // Scene / Resources
  // --------------------------------------------------
  gltf::Scene& gltfResources();
  const gltf::Scene& gltfResources() const;
  SceneResourcesManager& sceneResources();

  // --------------------------------------------------
  // Rendering parameters
  // --------------------------------------------------
  void setRenderMode(RenderMode mode);
  shaderio::TonemapperData& tonemapper();
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
  std::shared_ptr<ISceneRenderer> m_renderer = nullptr;
  glm::vec2 m_metallicRoughnessOverride{-0.01f, -0.01f};
};

;
