#pragma once

#include <volk/volk.h>

#include <nvvk/resources.hpp>

#include "backend/vulkan/core/vulkan_context_manager.hpp"
#include "renderer/interfaces/render_graph_interface.hpp"
#include "vulkan_scene_asset_manager.hpp"

#ifdef PROFILE_APP
#include "nvvk/profiler_vk.hpp"
#endif

namespace vkb
{

// Fixed shadow map resolution (2048×2048).
static constexpr uint32_t kShadowMapSize = 2048;

/**
 * @brief Renders the scene from the directional light's perspective into a
 *        depth-only image (the shadow map).
 *
 * The resulting image and its comparison sampler are exposed so that the
 * subsequent raster / meshlet passes can bind them for PCF shadow lookups.
 *
 * The shadow map image is allocated in the constructor so that downstream
 * passes can retrieve the pointer before init() is called by the RenderGraph.
 */
class VulkanShadowPass final : public IRenderPass
{
public:
  VulkanShadowPass(VulkanContextManager*        context,
                   const VulkanSceneAssetManager* assetManager);
  ~VulkanShadowPass() override = default;

  void init() override;
  void deinit() override;
  void setup(PassBuilder& builder) override;
  void execute(IRenderContext& ctx) override;
  std::string_view name() const override { return "Shadow"; }

#ifdef PROFILE_APP
  void setGpuTimer(nvvk::ProfilerGpuTimer* t) { m_gpuTimer = t; }
#endif

  // Accessors for downstream passes to bind the shadow map.
  const nvvk::Image& getShadowMap()     const { return m_shadowMap; }
  VkSampler          getCompareSampler() const { return m_compareSampler; }

private:
  void createShadowImage();
  void createCompareSampler();
  void createPipelineLayout();
  void compileShaders();
  void clearShaders();

  VulkanContextManager*          m_context      = nullptr;
  const VulkanSceneAssetManager* m_assetManager = nullptr;

  nvvk::Image m_shadowMap{};
  VkSampler   m_compareSampler{VK_NULL_HANDLE};

  VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
  VkShaderEXT      m_vertexShader{VK_NULL_HANDLE};

#ifdef PROFILE_APP
  nvvk::ProfilerGpuTimer* m_gpuTimer = nullptr;
#endif
};

}  // namespace vkb
