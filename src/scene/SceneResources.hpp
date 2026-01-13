#pragma once

#include <vulkan/vulkan.h>  // For VkCommandBuffer

#include <memory>
#include <string>

#include "backend/vulkan/VulkanSceneResources.hpp"
#include "scene/gltf/gltf_utils.hpp"

// Forward declarations to avoid heavy includes
namespace tinygltf
{
class Model;
}

class SceneResources
{
public:
  using InstanceID = uint32_t;
  using MaterialID = uint32_t;

  // Constructor/Destructor (defaults defined in CPP to handle forward declarations)
  SceneResources();
  ~SceneResources();

  void begin_uploading();
  void init(std::shared_ptr<VulkanSceneResources> gpu_uploader);
  void end_uploading();

  // We can return tinygltf::Model by value with a forward declaration
  // provided the caller includes tiny_gltf.h
  tinygltf::Model loadGltf(const std::string& filename);

  uint32_t loadTexture(const std::string& filename);

  InstanceID addInstance(const shaderio::GltfInstance& instance);
  MaterialID addMaterial(const shaderio::GltfMetallicRoughness& material);

  void finalizeSceneResources();
  void clear();

  const nvsamples::GltfSceneResource& data() const;
  nvsamples::GltfSceneResource& data();

  shaderio::GltfSceneInfo& scene_info();
  const shaderio::GltfSceneInfo& scene_info() const;

private:
  nvsamples::GltfSceneResource m_resources{};
  std::shared_ptr<VulkanSceneResources> m_gpu_uploader = nullptr;
};

;
