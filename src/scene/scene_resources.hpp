#pragma once

#include <vulkan/vulkan.h>

#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/sampler_pool.hpp>
#include <src/common/path_utils.hpp>
#include <vector>

#include "backend/vulkan/vulkan_scene_resources.hpp"
#include "scene/gltf/gltf_utils.hpp"

class CpuSceneResources
{

public:
  using InstanceID = uint;
  using MaterialID = uint;

  void init(std::shared_ptr<VulkanSceneResources> gpu_uploader) { m_gpu_uploader = gpu_uploader; }

  tinygltf::Model loadGltf(const std::string& filename)
  {
    auto model =
        nvsamples::loadGltfResources(nvutils::findFile(filename, nvsamples::getResourcesDirs()));
    auto id = static_cast<VulkanSceneResources::MeshID>(
        m_gpu_uploader->upload_gltf_model(model, m_resources));
    return model;
  }

  VulkanSceneResources::TextureID loadTexture(const std::string& filename, VkCommandBuffer cmd)
  {
    const auto imageFilename = nvutils::findFile(filename, nvsamples::getResourcesDirs());
    return m_gpu_uploader->upload_texture(imageFilename, cmd);
  }

  InstanceID addInstance(const shaderio::GltfInstance& instance)
  {
    m_resources.instances.push_back(instance);
    return m_resources.instances.size() - 1;
  }

  MaterialID addMaterial(const shaderio::GltfMetallicRoughness& material)
  {
    m_resources.materials.push_back(material);
    return m_resources.materials.size() - 1;
  }

  void finalizeSceneResources(VkCommandBuffer cmd)
  {
    m_gpu_uploader->finalizeSceneResources(m_resources, cmd);
  }
  void clear() { m_gpu_uploader->clear(m_resources); }

  const nvsamples::GltfSceneResource& data() const { return m_resources; };
  nvsamples::GltfSceneResource& data() { return m_resources; };

  shaderio::GltfSceneInfo& scene_info() { return m_resources.sceneInfo; }
  const shaderio::GltfSceneInfo& scene_info() const { return m_resources.sceneInfo; }

private:
  nvsamples::GltfSceneResource m_resources{};
  std::shared_ptr<VulkanSceneResources> m_gpu_uploader = nullptr;
};