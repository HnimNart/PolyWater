#pragma once

#include <vulkan/vulkan.h>

#include <memory>

#include "Acceleration.hpp"
#include "backend/interfaces/IDeviceAssets.hpp"
#include "backend/interfaces/IRenderGraph.hpp"
#include "backend/interfaces/IRenderer.hpp"
#include "backend/vulkan/core/ContextManager.hpp"
#include "passes/RasterPass.hpp"
#include "passes/RayTracePass.hpp"
#include "scene/SceneManager.hpp"
#include "scene/gltf/io_gltf.h"

class PostProcessor;
class IRenderBackend;
class VulkanSceneAssetManager;
class SceneResourcesManager;
class FrameSynchronizationManager;
class ToneMapPass;

namespace shaderio
{
struct PushConstant;
}

class VulkanRenderer final : public IRenderer
{
public:
  explicit VulkanRenderer(VulkanBackend* backend);
  ~VulkanRenderer() override = default;

  // Delete copy/move
  VulkanRenderer(const VulkanRenderer&) = delete;
  VulkanRenderer& operator=(const VulkanRenderer&) = delete;
  VulkanRenderer(VulkanRenderer&&) = delete;
  VulkanRenderer& operator=(VulkanRenderer&&) = delete;

  // ---------------------------------------------------------------------------
  // Lifecycle
  // ---------------------------------------------------------------------------
  void init(const SceneResourcesManager& scene) override;
  void deinit() override;
  void reload(const SceneResourcesManager& scene) override;
  void update(const SceneResourcesManager& scene) override;

  // ---------------------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------------------
  shaderio::GltfSceneInfo*
  updateSceneBuffer(VkCommandBuffer cmd,
                    shaderio::GltfSceneInfo& sceneInfo) const;
  void setRenderMode(RenderMode mode,
                     const SceneResourcesManager& scene) override;
  void render(IRenderContext& ctx) const override;
  void onResize(const WindowSize& size) override;
  void saveImage(const std::filesystem::path& filename,
                 int quality = 100) const override;

  // ---------------------------------------------------------------------------
  // Accessors
  // ---------------------------------------------------------------------------
  void* getImageDescriptor(RenderOutput output) const override;
  IToneMapper& postProcessor() noexcept override;
  std::shared_ptr<IDeviceAssets> deviceResources() noexcept override;

private:
  void initGBuffers();
  void buildGraph(const SceneResourcesManager& scene);
  void registerShaders();
  shaderio::GltfSceneInfo*
  updateSceneBuffer(VkCommandBuffer cmd,
                    const SceneResourcesManager& scene) const;
  void createDescriptorSetLayout(VkDevice device);

  // Data
  nvvk::DescriptorPack m_descPack{};
  VulkanContextManager* m_context_manager = nullptr;
  SwapchainRenderManager* m_swapchain_manager = nullptr;
  std::shared_ptr<VulkanSceneAssetManager> m_resources;
  std::unique_ptr<nvvk::GBuffer> m_gBuffers;

  std::unique_ptr<AccelerationStructures> m_accel{};

  // Reference to post for UI
  ToneMapPass* m_post = nullptr;

  // Render stuff
  RenderGraph m_graph;
  ShaderManager m_shaderManager;
};
