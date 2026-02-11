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

namespace nvvk {
class GBuffer;
}
namespace core {
class CameraManipulator;
}
namespace shaderio {
struct PushConstant;
}

class RasterPass : public IRenderPass {
public:
  RasterPass(nvvk::DescriptorPack *descPack);
  ~RasterPass() = default;

  void init(VulkanContextManager *coreManager) override;
  void deinit(VulkanContextManager *coreManager) override;

  void setup(PassBuilder &builder) override;

  // Raster //
  void execute(const IRenderContext &ctx) override;
  void reload();
  void resize(VkCommandBuffer cmd, VkExtent2D size);

  const nvvk::GBuffer &gbuffer() const;

private:
  void createDescriptorSetLayout(VkDevice device);
  void createPipelineLayout(VkDevice device);
  void clearShaders();
  void compileShaders();

  VulkanContextManager *m_context_manager = nullptr;
  nvvk::DescriptorPack *m_descPack = nullptr;
  VkPipelineLayout m_pipelineLayout{};

  VkShaderEXT m_vertexShader{};
  VkShaderEXT m_fragmentShader{};

  // Sky rendering helper
  nvshaders::SkySimple m_skySimple{};
};
