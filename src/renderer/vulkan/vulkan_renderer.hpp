#pragma once

#include <memory>

#include "renderer/interfaces/render_graph_interface.hpp"
#include "renderer/interfaces/renderer_interface.hpp"
#include "renderer/vulkan/passes/vulkan_ray_trace_pass.hpp"
#include "scene/scene_manager.hpp"
#include "vulkan_acceleration_structures.hpp"
#include "vulkan_pipeline_manager.hpp"

#ifdef PROFILE_APP
#include "nvvk/profiler_vk.hpp"
#endif

namespace scene
{
class SceneResourcesManager;
}  // namespace scene

namespace vkb
{

class VulkanBackend;
class VulkanFrameSynchronizationManager;
class VulkanToneMapPass;

class VulkanRenderer final : public IRenderer
{
public:
  explicit VulkanRenderer(
      VulkanBackend* backend,
      const std::vector<std::filesystem::path>& shaderPaths);
  ~VulkanRenderer() override = default;

  // Delete copy/move
  VulkanRenderer(const VulkanRenderer&) = delete;
  VulkanRenderer& operator=(const VulkanRenderer&) = delete;
  VulkanRenderer(VulkanRenderer&&) = delete;
  VulkanRenderer& operator=(VulkanRenderer&&) = delete;

  // ---------------------------------------------------------------------------
  // Lifecycle
  // ---------------------------------------------------------------------------
  void init(const scene::SceneResourcesManager& scene) override;
  void deinit() override;
  void clear();
  void reload() override;
  bool update(const scene::SceneResourcesManager& scene) override;

  // ---------------------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------------------
  void setRenderMode(const std::string& mode) override;
  void render(IRenderContext& ctx) override;
  void onResize(const WindowSize& size) override;
  void saveImage(const std::filesystem::path& filename,
                 int quality = 100) const override;

  // ---------------------------------------------------------------------------
  // Accessors
  // ---------------------------------------------------------------------------
  int64_t getImageDescriptor(RenderOutput output) const override;
  IToneMapper& postProcessor() noexcept override;
  std::shared_ptr<IDeviceAssets> deviceResources() noexcept override;
  std::vector<std::string> getAvaliableModes() const override;
  std::string getCurrentMode() const override;

  void setDenoise(bool value) final;

private:
  void initGBuffers();
  void buildGraph(const std::string& mode);
  void registerShaders();

  // Data
  VulkanContextManager* m_context{};
  VulkanFrameSynchronizationManager* m_frameSyncManager{};
  VulkanSwapchainRenderManager* m_swapchain_manager{};
  std::shared_ptr<VulkanSceneAssetManager> m_resources;
  std::unique_ptr<nvvk::GBuffer> m_gBuffers;
  std::unique_ptr<VulkanAccelerationStructures> m_accel{};

  nvvk::Image m_hiZTexture{};
  void initHiZBuffer(VkCommandBuffer cmd, VkExtent2D size);
  void destroyHiZBuffer();

  // Reference to post for UI
  std::unique_ptr<RenderGraph> m_graph;
  VulkanPipelineManager m_pipelineManager;

#ifdef PROFILE_APP
  nvvk::ProfilerGpuTimer* m_gpuTimer = nullptr;
#endif
};
}  // namespace vkb
