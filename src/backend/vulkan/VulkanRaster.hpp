#pragma once

#include <vulkan/vulkan.h>

#include <memory>

#include <nvshaders_host/sky.hpp>  // Needed for nvshaders::SkySimple member
#include <nvvk/descriptors.hpp>

// Forward declarations to avoid heavy includes
struct VulkanContext;
class SlangShaderCompiler;
class CpuSceneResources;

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
  void init(std::shared_ptr<VulkanContext> ctx);
  void deinit();

  // Note: implementation moved to cpp
  void resize(VkCommandBuffer cmd, VkExtent2D size);

  // Raster //
  //---------------------------------------------------------------------------------------------------------------
  // Recording the commands to render the scene
  //
  void render(VkCommandBuffer cmd, const nvvk::GBuffer& gBuffers, const CpuSceneResources& scene,
              const std::shared_ptr<nvutils::CameraManipulator>& camera,
              shaderio::PushConstant& push_constants) const;

  void reload();

  const nvvk::GBuffer& gbuffer()
      const;  // Forward declaration implied (implementation needed in cpp if accessing members)
  nvvk::DescriptorPack& descPack() { return m_descPack; }

private:
  void createDescriptorSetLayout(VkDevice device);
  void createPipelineLayout(VkDevice device);
  void clearShaders(std::shared_ptr<VulkanContext> ctx);
  void compileShaders(std::shared_ptr<VulkanContext> ctx);

private:
  std::shared_ptr<VulkanContext> m_ctx = nullptr;
  std::shared_ptr<SlangShaderCompiler> m_compiler = nullptr;

  nvvk::DescriptorPack m_descPack{};
  VkPipelineLayout m_pipelineLayout{};

  VkShaderEXT m_vertexShader{};
  VkShaderEXT m_fragmentShader{};

  // Sky rendering helper
  nvshaders::SkySimple m_skySimple{};
};
