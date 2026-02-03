#pragma once

#include <vulkan/vulkan.h>

#include <memory>

#include <nvshaders_host/sky.hpp>
#include <nvvk/descriptors.hpp>

#include "backend/interfaces/IRenderGraph.hpp"
#include "backend/vulkan/core/Backend.hpp"
#include "scene/gltf/gltf_utils.hpp"

// Forward declarations to avoid heavy includes

class VulkanSceneGpuData;

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

class VulkanRaster : public IRenderPass
{
public:
  VulkanRaster(nvvk::DescriptorPack* descPack);
  ~VulkanRaster() = default;

  void init(VulkanContextManager* coreManager,
            const SceneResourcesManager& scene) override;
  void deinit(VulkanContextManager* coreManager) override;

  // Raster //
  //---------------------------------------------------------------------------------------------------------------
  // Recording the commands to render the scene
  //
  void execute(const IRenderContext& ctx) override;

  void reload();
  void resize(VkCommandBuffer cmd, VkExtent2D size);

  const nvvk::GBuffer& gbuffer() const;

private:
  void createDescriptorSetLayout(VkDevice device);
  void createPipelineLayout(VkDevice device);
  void clearShaders();
  void compileShaders();

  VulkanContextManager* m_core_manager = nullptr;
  nvvk::DescriptorPack* m_descPack = nullptr;
  VkPipelineLayout m_pipelineLayout{};

  VkShaderEXT m_vertexShader{};
  VkShaderEXT m_fragmentShader{};

  // Sky rendering helper
  nvshaders::SkySimple m_skySimple{};
};
