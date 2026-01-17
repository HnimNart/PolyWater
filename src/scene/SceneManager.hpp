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
  void post_process();
  void reload(bool use_raytracing);
  core::Image get_image(int buffer_idx);
  void onResize(const WindowSize& size);

  // --------------------------------------------------
  // Scene / Resources
  // --------------------------------------------------
  nvsamples::GltfSceneResource& gltf_resources();
  const nvsamples::GltfSceneResource& gltf_resources() const;
  SceneResourcesManager& scene_resources();

  // --------------------------------------------------
  // Rendering parameters
  // --------------------------------------------------
  shaderio::TonemapperData& tonemapper();
  glm::vec2& metallic_roughness();

  // --------------------------------------------------
  // Camera
  // --------------------------------------------------
  void set_camera(CameraPtr camera);
  CameraPtr camera() const;

private:
  CameraPtr m_camera{std::make_shared<nvutils::CameraManipulator>()};
  SceneResourcesManager m_scene_resources{};
  std::shared_ptr<ISceneRenderer> m_renderer = nullptr;
  glm::vec2 m_metallicRoughnessOverride{-0.01f, -0.01f};
};

;
