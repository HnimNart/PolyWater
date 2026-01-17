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

  void init(std::shared_ptr<IDeviceResources> deviceResources);
  void beginUploading();
  void endUploading();

  tinygltf::Model loadGltf(const std::string& filename);
  IDeviceResources::TextureID loadTexture(const std::string& filename);
  InstanceID addInstance(const shaderio::GltfInstance& instance);
  MaterialID addMaterial(const shaderio::GltfMetallicRoughness& material);

  void updateSceneInfo(const CameraPtr& camera);
  void finalizeSceneResources();
  void clear();

  const gltf::Scene& data() const;
  gltf::Scene& data();

  shaderio::GltfSceneInfo& sceneInfo();
  const shaderio::GltfSceneInfo& sceneInfo() const;

private:
  gltf::Scene m_resources{};
  std::shared_ptr<IDeviceResources> m_device_resources = nullptr;
};

;
