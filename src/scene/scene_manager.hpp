#pragma once

#include <memory>

#include <core/camera.hpp>
#include <glm/glm.hpp>

#include "scene_data.hpp"
#include "scene_resources.hpp"
#include "core/camera.hpp"

namespace shaderio
{
struct TonemapperData;
}

class IRenderContext;

namespace scene
{

class SceneManager
{
public:
  SceneManager() = default;
  explicit SceneManager(std::shared_ptr<IDeviceAssets> deviceResources);
  void clear();

  void buildSceneFromData(const SceneData& data,
                          const std::vector<std::filesystem::path>& searchDirs);
  // --------------------------------------------------
  // Scene / Resources
  // --------------------------------------------------
  void onPreRender();
  Scene* getScenePtr();
  Scene& gltfResources();
  const Scene& gltfResources() const;
  SceneResourcesManager& sceneResourceManager();
  const SceneResourcesManager& sceneResourceManager() const;

  void onEndFrame();

  // --------------------------------------------------
  // Rendering parameters
  // --------------------------------------------------
  shaderio::SceneInfo& sceneInfo();

  // --------------------------------------------------
  // Camera
  // --------------------------------------------------
  void setCamera(CameraPtr camera);
  CameraPtr camera() const;

private:
  CameraPtr m_camera{std::make_shared<core::CameraManipulator>()};
  SceneResourcesManager m_scene_resources{};

  bool has_changed = false;
};

}  // namespace scene
