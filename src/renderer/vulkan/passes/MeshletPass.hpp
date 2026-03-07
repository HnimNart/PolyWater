#pragma once

#include <vulkan/vulkan.h>

#include "renderer/interfaces/IRenderGraph.hpp"
#include <nvvk/descriptors.hpp>

// Forward declarations
struct VulkanSceneGpuData;

namespace nvvk {
class GBuffer;
}
namespace core {
class CameraManipulator;
}
namespace shaderio {
struct PushConstant;
}

class MeshletPass : public IRenderPass {
public:
  MeshletPass(const nvvk::DescriptorPack &descPack);
  ~MeshletPass() = default;

  void init(VulkanContextManager *coreManager) override;
  void deinit(VulkanContextManager *coreManager) override;

  void setup(PassBuilder &builder) override;

  // Render Execution
  void execute(const IRenderContext &ctx) override;
  void reload();
  void resize(VkCommandBuffer cmd, VkExtent2D size);

private:
  void createPipelineLayout(VkDevice device);
  void clearShaders();
  void compileShaders();

  VulkanContextManager *m_context_manager = nullptr;
  const nvvk::DescriptorPack &m_descPack;
  VkPipelineLayout m_pipelineLayout{};

  VkShaderEXT m_taskShader{};
  VkShaderEXT m_meshShader{};
  VkShaderEXT m_fragmentShader{};
};
