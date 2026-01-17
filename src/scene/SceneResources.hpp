#pragma once

#include <memory>
#include <string>

#include "backend/IDeviceResources.hpp"
#include "core/Camera.hpp"
#include "scene/gltf/gltf_utils.hpp"

namespace tinygltf
{
class Model;
}

class SceneResourcesManager
{
public:
  using InstanceID = uint32_t;
  using MaterialID = uint32_t;

  // Constructor/Destructor
  SceneResourcesManager();
  ~SceneResourcesManager();

  void init(std::shared_ptr<IDeviceResources> gpu_uploader);
  void begin_uploading();
  void end_uploading();

  tinygltf::Model loadGltf(const std::string& filename);
  uint32_t loadTexture(const std::string& filename);
  InstanceID addInstance(const shaderio::GltfInstance& instance);
  MaterialID addMaterial(const shaderio::GltfMetallicRoughness& material);

  void update_scene_info(CameraPtr camera);
  void finalizeSceneResources();
  void clear();

  const gltf::Scene& data() const;
  gltf::Scene& data();

  shaderio::GltfSceneInfo& scene_info();
  const shaderio::GltfSceneInfo& scene_info() const;

private:
  gltf::Scene m_resources{};
  std::shared_ptr<IDeviceResources> m_device_resources = nullptr;
};

;
