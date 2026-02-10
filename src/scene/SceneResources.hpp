#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "backend/interfaces/IDeviceAssets.hpp"
#include "core/Camera.hpp"
#include "scene/Scene.h"
#include "scene/gltf/gltf_utils.hpp"
#include "tiny_gltf.h"

using InstanceID = uint32_t;
using MaterialID = uint32_t;
using MeshID = IDeviceAssets::MeshID;
using TextureID = IDeviceAssets::TextureID;

class SceneResourcesManager {
public:
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
  MeshID loadGltf(const std::string &name, const std::string &filename);
  TextureID loadTexture(const std::string &name, const std::string &filename);

  // ---------------------------------------------------------------------------
  // Scene Composition
  // ---------------------------------------------------------------------------
  InstanceID addInstance(shaderio::Instance &&instance, std::string = "");
  MaterialID addMaterial(shaderio::Material &&material, std::string = "");

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

  const shaderio::SceneInfo &sceneInfo() const;
  shaderio::SceneInfo &sceneInfo();
  void setSceneInfo(shaderio::SceneInfo sceneInfo);
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

  const shaderio::MeshPrimitive &getMeshFromIdx(uint32_t index) const {
    assert(m_resources.meshes.size() < index);
    return m_resources.meshes[index];
  }

  const std::map<std::string, MaterialID> &materialMap() const {
    return m_materialMap;
  }
  const std::map<std::string, InstanceID> &instanceMap() const {
    return m_instanceMap;
  }

  shaderio::Material &getMaterialFromName(const std::string &name) {
    auto it = m_materialMap.find(name);
    if (it != m_materialMap.end()) {
      return m_resources.materials[it->second];
    }
    assert(0);
  }

  const MeshID getMeshIDFromName(const std::string &name) const {
    auto it = m_meshMap.find(name);
    if (it != m_meshMap.end()) {
      return it->second;
    }
    return MeshID(-1);
  }

  const TextureID getTextureIDFromName(const std::string &name) const {
    auto it = m_textureMap.find(name);
    if (it != m_textureMap.end()) {
      return it->second;
    }
    return TextureID(-1);
  }

private:
  Scene m_resources{};
  std::shared_ptr<IDeviceAssets> m_device_resources = nullptr;

  // Things to be uploaded to gpu
  std::vector<tinygltf::Model> m_pendingModels{};
  struct PendingTexture {
    std::string filename;
    TextureID id;
  };
  std::vector<PendingTexture> m_pendingTextures{};

  std::map<std::string, MaterialID> m_materialMap{};
  std::map<std::string, InstanceID> m_instanceMap{};
  std::map<std::string, MeshID> m_meshMap{};
  std::map<std::string, TextureID> m_textureMap{};

  bool m_dirty = false;
};
