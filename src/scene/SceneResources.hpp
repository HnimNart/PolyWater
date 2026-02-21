#pragma once

#include <filesystem>
#include <fmt/format.h>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "Scene.h"
#include "core/Camera.hpp"
#include "lights/LightManager.hpp"
#include "models/optimizer.hpp"
#include "renderer/interfaces/IDeviceAssets.hpp"

using InstanceID = uint32_t;
using MaterialID = uint32_t;
using MeshID = IDeviceAssets::MeshID;
using TextureID = IDeviceAssets::TextureID;

class SceneResourcesManager {
public:
  // --- Lifecycle ---
  SceneResourcesManager() = default;
  void init(std::shared_ptr<IDeviceAssets> deviceResources);
  void clear();

  // --- Asset Loading & Deduplication ---
  std::vector<MeshID> loadModel(const std::string &name,
                                const std::string &filename);
  TextureID addTexture(const std::string &name, const std::string &filename);
  void addEnvmap(const std::filesystem::path &filename, float scale = 1.0f,
                 float rotation = 0.0f);

  // Process queued CPU assets and move to GPU
  void finalizeSceneResources();

  // --- Scene Composition ---
  InstanceID addInstance(shaderio::Instance &&instance, std::string name = "");
  MaterialID addMaterial(shaderio::Material &&material, std::string name = "");
  void setSceneInfo(shaderio::SceneInfo sceneInfo);

  // --- Runtime Updates ---
  void update(const CameraPtr &camera);
  void updateSceneInfo(const CameraPtr &camera);

  void onMaterialChange();
  void onInstanceChange();

  // --- State Management ---
  bool dirty() const { return m_dirty; }
  void setDirty(bool val) { m_dirty = val; }

  // --- Data Accessors (Mutable) ---
  Scene &data() { return m_resources; }
  shaderio::SceneInfo &sceneInfo() { return m_resources.sceneInfo; }

  std::vector<shaderio::Instance> &getInstances() {
    return m_resources.instances;
  }
  std::vector<shaderio::Material> &getMaterials() {
    return m_resources.materials;
  }

  // --- Data Accessors (Const) ---
  const Scene &data() const { return m_resources; }
  const shaderio::SceneInfo &sceneInfo() const { return m_resources.sceneInfo; }

  const std::vector<shaderio::Instance> &getInstances() const {
    return m_resources.instances;
  }
  const std::vector<shaderio::Material> &getMaterials() const {
    return m_resources.materials;
  }
  const std::vector<shaderio::MeshPrimitive> &getMeshes() const {
    return m_resources.meshes;
  }

  // --- Map Accessors (Name Lookups) ---
  const std::unordered_map<std::string, MaterialID> &materialMap() const {
    return m_materialMap;
  }
  const std::unordered_map<std::string, InstanceID> &instanceMap() const {
    return m_instanceMap;
  }
  const std::unordered_map<std::string, MeshID> &meshMap() const {
    return m_meshMap;
  }
  const std::unordered_map<std::string, TextureID> &textureMap() const {
    return m_textureMap;
  }

  // --- Safe Resource Fetching ---
  shaderio::Material *getMaterialFromName(const std::string &name);
  MeshID getMeshIDFromName(const std::string &name) const;
  TextureID getTextureIDFromName(const std::string &name) const;
  const shaderio::MeshPrimitive &getMeshFromIdx(uint32_t index) const;

private:
  // Internal Loading Helpers
  std::vector<MeshID> loadGltf(const std::string &name,
                               const std::string &filename);
  std::vector<MeshID> loadObj(const std::string &name,
                              const std::string &filename);

  void uploadOptimizedMesh(const OptimizedPayload &payload);
  void uploadLights();
  void uploadTextures();
  MeshID getNextFreeMeshID();

  // CPU Side Storage for Pending GPU Uploads
  struct PendingMeshTask {
    enum class Type { GLTF, PRIMITIVE } type;
    size_t index;
  };

  struct PendingEnvMap {
    std::filesystem::path filepath;
    float scale = 1.0f;
    float rotation = 0.0f;
  };

  struct PendingTexture {
    std::string filename;
    TextureID id;
  };

  // Scene State
  Scene m_resources{};
  std::shared_ptr<IDeviceAssets> m_device_resources = nullptr;
  LightManager m_lights;
  bool m_dirty = false;

  // Queues
  std::vector<OptimizedPayload> m_pendingOptimizedMesh;
  std::vector<PendingTexture> m_pendingTextures;
  std::optional<PendingEnvMap> m_pendingEnvmap;

  // Deduplication and Naming Maps
  std::unordered_map<std::string, MaterialID> m_materialMap;
  std::unordered_map<std::string, InstanceID> m_instanceMap;
  std::unordered_map<std::string, MeshID> m_meshMap;
  std::unordered_map<std::string, TextureID> m_textureMap;
  std::unordered_map<std::filesystem::path, TextureID> m_fileToTextureMap;
  uint m_pendingMeshes{0};
};
