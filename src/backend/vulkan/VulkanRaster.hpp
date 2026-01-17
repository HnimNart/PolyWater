#pragma once

#include <vulkan/vulkan.h>

#include <memory>

#include <nvshaders_host/sky.hpp>
#include <nvvk/descriptors.hpp>

#include "VulkanBackend.hpp"
#include "scene/gltf/gltf_utils.hpp"

// Forward declarations to avoid heavy includes

class GltfDeviceSceneResources;

namespace nvvk
{
class GBuffer;
}
namespace nvutils
{
class CameraManipulator;
}
namespace shaderio
{
struct PushConstant;
}

class VulkanRaster
{
public:
  VulkanRaster() = default;
  void init(core::VulkanBackend* backend);
  void deinit();
  void resize(VkCommandBuffer cmd, VkExtent2D size);

  // Raster //
  //---------------------------------------------------------------------------------------------------------------
  // Recording the commands to render the scene
  //
  void render(VkCommandBuffer cmd, const nvvk::GBuffer& gBuffers, const gltf::Scene& sceneResources,
              const GltfDeviceSceneResources& deviceResources,
              const std::shared_ptr<nvutils::CameraManipulator>& camera,
              shaderio::PushConstant& pushConstants) const;

  void reload();

  const nvvk::GBuffer& gbuffer() const;
  nvvk::DescriptorPack& descPack() { return m_descPack; }

private:
  void createDescriptorSetLayout(VkDevice device);
  void createPipelineLayout(VkDevice device);
  void clearShaders();
  void compileShaders();

private:
  core::VulkanBackend* m_backend = nullptr;

  nvvk::DescriptorPack m_descPack{};
  VkPipelineLayout m_pipelineLayout{};

  VkShaderEXT m_vertexShader{};
  VkShaderEXT m_fragmentShader{};

  // Sky rendering helper
  nvshaders::SkySimple m_skySimple{};
};
