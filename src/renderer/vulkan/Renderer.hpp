#pragma once

#include <vulkan/vulkan.h>

#include <memory>

#include "Acceleration.hpp"
#include "backend/vulkan/core/ContextManager.hpp"
#include "passes/RasterPass.hpp"
#include "passes/RayTracePass.hpp"
#include "renderer/interfaces/IDeviceAssets.hpp"
#include "renderer/interfaces/IRenderGraph.hpp"
#include "renderer/interfaces/IRenderer.hpp"
#include "scene/SceneManager.hpp"

class PostProcessor;
class IRenderBackend;
class VulkanSceneAssetManager;
class SceneResourcesManager;
class FrameSynchronizationManager;
class SwapchainRenderManager;
class ToneMapPass;
class VulkanBackend;

class VulkanRenderer final : public IRenderer {
public:
  explicit VulkanRenderer(
      VulkanBackend *backend,
      const std::vector<std::filesystem::path> &shaderPaths);
  ~VulkanRenderer() override = default;

  // Delete copy/move
  VulkanRenderer(const VulkanRenderer &) = delete;
  VulkanRenderer &operator=(const VulkanRenderer &) = delete;
  VulkanRenderer(VulkanRenderer &&) = delete;
  VulkanRenderer &operator=(VulkanRenderer &&) = delete;

  // ---------------------------------------------------------------------------
  // Lifecycle
  // ---------------------------------------------------------------------------
  void init(const SceneResourcesManager &scene) override;
  void deinit() override;
  void clear();
  void reload() override;
  bool update(const SceneResourcesManager &scene) override;

  // ---------------------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------------------
  void setRenderMode(RenderMode mode) override;
  void render(IRenderContext &ctx) override;
  void onResize(const WindowSize &size) override;
  void saveImage(const std::filesystem::path &filename,
                 int quality = 100) const override;

  // ---------------------------------------------------------------------------
  // Accessors
  // ---------------------------------------------------------------------------
  int64_t getImageDescriptor(RenderOutput output) const override;
  IToneMapper &postProcessor() noexcept override;
  std::shared_ptr<IDeviceAssets> deviceResources() noexcept override;

private:
  void initGBuffers();
  void buildGraph();
  void registerShaders();

  // Data
  VulkanContextManager *m_context{};
  SwapchainRenderManager *m_swapchain_manager{};
  std::shared_ptr<VulkanSceneAssetManager> m_resources;
  std::unique_ptr<nvvk::GBuffer> m_gBuffers;
  std::unique_ptr<AccelerationStructures> m_accel{};
  RenderGraph m_graph;

  // Reference to post for UI
  ToneMapPass *m_post = nullptr;
};
