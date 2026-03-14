#pragma once

#include <vulkan/vulkan.h>

#include "SceneAssetManager.hpp"
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

class RasterPass : public IRenderPass {
public:
  RasterPass(const nvvk::DescriptorPack &descPack,
             const VulkanSceneAssetManager *assetManager);
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
  void createPipelineLayout(VkDevice device);
  void clearShaders();
  void compileShaders();

  VulkanContextManager *m_context_manager = nullptr;
  const VulkanSceneAssetManager *m_assetManager = nullptr;
  const nvvk::DescriptorPack &m_descPack;
  VkPipelineLayout m_pipelineLayout{};

  VkShaderEXT m_vertexShader{};
  VkShaderEXT m_fragmentShader{};
};
