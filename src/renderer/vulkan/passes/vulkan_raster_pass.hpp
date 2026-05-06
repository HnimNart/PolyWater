#pragma once

#include <vulkan/vulkan.h>

#include <nvvk/descriptors.hpp>

#include "vulkan_scene_asset_manager.hpp"
#include "renderer/interfaces/render_graph_interface.hpp"

#ifdef PROFILE_APP
#include "nvvk/profiler_vk.hpp"
#endif

// Forward declarations
struct VulkanSceneGpuData;

namespace nvvk
{
class GBuffer;
}
namespace core
{
class CameraManipulator;
}
namespace shaderio
{
struct PushConstant;
}

class VulkanRasterPass final : public IRenderPass
{
public:
  VulkanRasterPass(VulkanContextManager* contextManager,
             const nvvk::DescriptorPack& descPack,
             const VulkanSceneAssetManager* assetManager);
  ~VulkanRasterPass() = default;

  void init() override;
  void deinit() override;

  void setup(PassBuilder& builder) override;

  // Raster //
  void execute(IRenderContext& ctx) override;
  std::string_view name() const override { return "Raster"; }
#ifdef PROFILE_APP
  void setGpuTimer(nvvk::ProfilerGpuTimer* t) { m_gpuTimer = t; }
#endif
  void reload();
  void resize(VkCommandBuffer cmd, VkExtent2D size);

  const nvvk::GBuffer& gbuffer() const;

private:
  void createPipelineLayout(VkDevice device);
  void clearShaders();
  void compileShaders();

  VulkanContextManager* m_context_manager = nullptr;
  const VulkanSceneAssetManager* m_assetManager = nullptr;
  const nvvk::DescriptorPack& m_descPack;
  VkPipelineLayout m_pipelineLayout{};

  VkShaderEXT m_vertexShader{};
  VkShaderEXT m_fragmentShader{};
#ifdef PROFILE_APP
  nvvk::ProfilerGpuTimer* m_gpuTimer = nullptr;
#endif
};
