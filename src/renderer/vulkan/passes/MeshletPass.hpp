#pragma once

#include <vulkan/vulkan.h>

#include <nvvk/descriptors.hpp>

#include "backend/vulkan/core/ContextManager.hpp"
#include "renderer/interfaces/IRenderGraph.hpp"

// Forward declarations
struct VulkanSceneGpuData;

namespace nvvk
{
class GBuffer;
class ResourceAllocator;
}  // namespace nvvk
namespace core
{
class CameraManipulator;
}
namespace shaderio
{
struct PushConstant;
}

class MeshletPass : public IRenderPass
{
public:
  MeshletPass(VulkanContextManager* contextManager,
              const nvvk::DescriptorPack& descPack,
              const nvvk::Image* hiZtexture);
  ~MeshletPass() = default;

  void init() override;
  void deinit() override;

  void setup(PassBuilder& builder) override;

  // Render Execution
  void execute(const IRenderContext& ctx) override;
  void reload();
  void resize(VkCommandBuffer cmd, VkExtent2D size);

private:
  void createPipelineLayout(VkDevice device);
  void clearShaders();
  void compileShaders();
  void allocateDynamicBuffers(nvvk::ResourceAllocator& allocator);

  VulkanContextManager* m_context_manager = nullptr;
  const nvvk::DescriptorPack& m_descPack;
  nvvk::DescriptorPack m_passDescPack;
  VkPipelineLayout m_pipelineLayout{};
  const nvvk::Image* m_hiZTexture = nullptr;

  VkShaderEXT m_taskShader{};
  VkShaderEXT m_meshShader{};
  VkShaderEXT m_fragmentShader{};

  // Add to your class members
  static constexpr uint32_t FRAMES_IN_FLIGHT = 3;
  nvvk::Buffer m_globalMeshletRefsBuffers[FRAMES_IN_FLIGHT];
  uint32_t m_currentFrameIndex = 0;  // Tracks which buffer to use this frame
};
