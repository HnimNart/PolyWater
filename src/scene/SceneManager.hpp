#pragma once

#include <memory>

#include <glm/glm.hpp>
#include <nvutils/camera_manipulator.hpp>

#include "SceneResources.hpp"
#include "backend/ISceneRenderer.hpp"
#include "core/Camera.hpp"
#include "core/Image.hpp"

class SceneManager
{
public:
  SceneManager() = default;
  explicit SceneManager(std::shared_ptr<ISceneRenderer> renderer);

  void clear();
  void postInit();
  void render(bool raytrace);

  // --------------------------------------------------
  // Rendering / Post-processing
  // --------------------------------------------------
  void postProcess();
  void reload(bool useRaytracing);
  core::Image getImage(int bufferIdx);
  core::Image getTonemapedImage();
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
