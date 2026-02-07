#pragma once

#include <memory>
#include <string>

#include "ShaderManager.hpp"
#include "backend/interfaces/IDeviceAssets.hpp"
#include "core/Camera.hpp"
#include "scene/Scene.h"
#include "scene/gltf/gltf_utils.hpp"
#include "tiny_gltf.h"

class SceneResourcesManager {
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
  tinygltf::Model loadGltf(const std::string &filename);
  IDeviceAssets::TextureID loadTexture(const std::string &filename);

  // ---------------------------------------------------------------------------
  // Scene Composition
  // ---------------------------------------------------------------------------
  InstanceID addInstance(shaderio::Instance &&instance);
  MaterialID addMaterial(shaderio::Material &&material);

  // ---------------------------------------------------------------------------
  // Runtime Updates
  // ---------------------------------------------------------------------------
  void update(const CameraPtr &camera);
  void updateSceneInfo(const CameraPtr &camera);

  bool dirty() const { return m_dirty; }
  void setDirty(bool val) { m_dirty = val; }

  void onMaterialChange();
  void onInstanceChange();

  // ---------------------------------------------------------------------------
  // Accessors
  // ---------------------------------------------------------------------------
  const Scene &data() const;
  Scene &data();

  shaderio::SceneInfo &sceneInfo();
  const shaderio::SceneInfo &sceneInfo() const;
  const std::vector<shaderio::Instance> &getInstances() const {
    return m_resources.instances;
  }
  std::vector<shaderio::Instance> &getInstances() {
    return m_resources.instances;
  }
  const std::vector<shaderio::Material> &getMaterials() const {
    return m_resources.materials;
  }
  std::vector<shaderio::Material> &getMaterials() {
    return m_resources.materials;
  }

private:
  Scene m_resources{};
  std::shared_ptr<IDeviceAssets> m_device_resources = nullptr;

  // Things to be uploaded to gpu
  std::vector<tinygltf::Model> m_pendingModels{};

  struct PendingTexture {
    std::string filename;
    IDeviceAssets::TextureID id;
  };
  std::vector<PendingTexture> m_pendingTextures{};

  bool m_dirty = false;
};
