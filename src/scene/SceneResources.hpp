#pragma once

#include <memory>
#include <string>

#include "backend/interfaces/IDeviceAssets.hpp"
#include "core/Camera.hpp"
#include "scene/gltf/gltf_utils.hpp"
#include "tiny_gltf.h"

class SceneResourcesManager
{
public:
  using InstanceID = uint32_t;
  using MaterialID = uint32_t;

  // ---------------------------------------------------------------------------
  // Lifecycle & Initialization
  // ---------------------------------------------------------------------------
  SceneResourcesManager() = default;
  ~SceneResourcesManager() = default;

  void init(std::shared_ptr<IDeviceAssets> deviceResources);
  void clear();

  // ---------------------------------------------------------------------------
  // Upload Transaction (Batching)
  // ---------------------------------------------------------------------------
  void finalizeSceneResources();

  // ---------------------------------------------------------------------------
  // Asset Loading (IO)
  // ---------------------------------------------------------------------------
  tinygltf::Model loadGltf(const std::string& filename);
  IDeviceAssets::TextureID loadTexture(const std::string& filename);

  // ---------------------------------------------------------------------------
  // Scene Composition
  // ---------------------------------------------------------------------------
  InstanceID addInstance(const shaderio::GltfInstance& instance);
  MaterialID addMaterial(const shaderio::GltfMetallicRoughness& material);

  // ---------------------------------------------------------------------------
  // Runtime Updates
  // ---------------------------------------------------------------------------
  void updateSceneInfo(const CameraPtr& camera);

  // ---------------------------------------------------------------------------
  // Accessors
  // ---------------------------------------------------------------------------
  const gltf::Scene& data() const;
  gltf::Scene& data();

  shaderio::GltfSceneInfo& sceneInfo();
  const shaderio::GltfSceneInfo& sceneInfo() const;

private:
  gltf::Scene m_resources{};
  std::shared_ptr<IDeviceAssets> m_device_resources = nullptr;

  // Things to be uploaded to gpu
  std::vector<tinygltf::Model> m_pendingModels{};

  struct PendingTexture
  {
    std::string filename;
    IDeviceAssets::TextureID id;
  };
  std::vector<PendingTexture> m_pendingTextures{};
};
