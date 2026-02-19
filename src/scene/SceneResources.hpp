#pragma once

#include <fmt/format.h>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "core/Camera.hpp"
#include "renderer/interfaces/IDeviceAssets.hpp"

#include "Scene.h"
#include "gltf/gltf_utils.hpp"
#include "lights/LightManager.hpp"

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
  std::vector<MeshID> loadModel(const std::string &name,
                                const std::string &filename);
  TextureID addTexture(const std::string &name, const std::string &filename);
  void addEnvmap(const std::filesystem::path &filename, float scale = 1.0f,
                 float rotation = 0.0f);

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

  const std::vector<shaderio::MeshPrimitive> &getMeshes() {
    return m_resources.meshes;
  }

  const std::map<std::string, MaterialID> &materialMap() const {
    return m_materialMap;
  }
  const std::map<std::string, InstanceID> &instanceMap() const {
    return m_instanceMap;
  }

  const std::map<std::string, MeshID> &meshMap() const { return m_meshMap; }

  shaderio::Material *getMaterialFromName(const std::string &name) {
    auto it = m_materialMap.find(name);
    if (it != m_materialMap.end()) {
      return &m_resources.materials[it->second];
    }
    return nullptr;
  }

  const shaderio::MeshPrimitive &getMeshFromIdx(uint32_t index) const {
    assert(m_resources.meshes.size() < index);
    return m_resources.meshes[index];
  }

  MeshID getMeshIDFromName(const std::string &name) const {
    auto it = m_meshMap.find(name);
    if (it != m_meshMap.end()) {
      return it->second;
    }

    throw std::runtime_error(fmt::format(
        "[SceneResourcesManager] Error: Mesh name '{}' not found in mesh map.",
        name));
    return MeshID(-1);
  }

  TextureID getTextureIDFromName(const std::string &name) const {
    auto it = m_textureMap.find(name);
    if (it != m_textureMap.end()) {
      return it->second;
    }
    throw std::runtime_error(
        fmt::format("[SceneResourcesManager] Error: Texture name '{}' not "
                    "found in mesh map.",
                    name));
    return TextureID(-1);
  }

private:
  Scene m_resources{};
  std::shared_ptr<IDeviceAssets> m_device_resources = nullptr;

  MeshID getNextFreeMeshID();

  std::vector<MeshID> loadGltf(const std::string &name,
                               const std::string &filename);
  std::vector<MeshID> loadObj(const std::string &name,
                              const std::string &filename);

  void uploadGltfMesh(const tinygltf::Model &model);
  void uploadPrimitiveMesh(const core::PrimitiveMesh &meshData);
  void uploadLights();
  void finalizePendingTextures();

  // Things to be uploaded to gpu
  struct PendingMeshTask {
    enum class Type { GLTF, PRIMITIVE };
    Type type;
    size_t index; // Index into m_pendingGltfModels or m_pendingPrimitives
  };

  struct PendingEnvMap {
    std::filesystem::path filepath;
    float scale = 1.0f;
    float rotation = 0.0f;
  };

  std::vector<PendingMeshTask> m_loadOrder;
  std::vector<core::PrimitiveMesh> m_pendingPrimitives{};
  std::vector<tinygltf::Model> m_pendingGltfModels{};
  uint m_pendingMeshes = 0;
  struct PendingTexture {
    std::string filename;
    TextureID id;
  };
  std::vector<PendingTexture> m_pendingTextures{};
  std::optional<PendingEnvMap> m_pendingEnvmap;

  std::map<std::string, MaterialID> m_materialMap{};
  std::map<std::string, InstanceID> m_instanceMap{};
  std::map<std::string, MeshID> m_meshMap{};
  std::map<std::string, TextureID> m_textureMap{};

  bool m_dirty = false;

  LightManager m_lights;
};
