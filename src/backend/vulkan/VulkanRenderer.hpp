#pragma once

#include <vulkan/vulkan.h>

#include <memory>

#include "VulkanAcceleration.hpp"
#include "VulkanBackend.hpp"
#include "VulkanPostToneMapper.hpp"
#include "VulkanRaster.hpp"
#include "VulkanRayTracer.hpp"
#include "scene/gltf/io_gltf.h"
#include "src/backend/ISceneRenderer.hpp"

class PostProcessor;
class IRenderBackend;
class VulkanRenderResources;
class SceneResourcesManager;

namespace shaderio
{
struct PushConstant;
}

class VulkanRenderer final : public ISceneRenderer
{
public:
  explicit VulkanRenderer(core::VulkanBackend* backend);
  ~VulkanRenderer() override;

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
  void reload(bool useRaytracing) override;

  // ---------------------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------------------
  shaderio::GltfSceneInfo* updateSceneBuffers(SceneResourcesManager& scene) override;
  void render(CameraPtr camera, const SceneResourcesManager& scene, bool raytrace,
              shaderio::PushConstant& pushValues) const override;

  void postProcess() override;
  void onResize(const WindowSize& size) override;

  // ---------------------------------------------------------------------------
  // Accessors
  // ---------------------------------------------------------------------------
  core::Image getImage(uint32_t index) const override;
  IPostProcessor& postProcessor() noexcept override;
  std::shared_ptr<IDeviceResources> deviceResources() noexcept override;

private:
  void initGBuffers();
  shaderio::GltfSceneInfo* updateSceneBuffer(VkCommandBuffer cmd,
                                             SceneResourcesManager& scene) const;
  void createDescriptorSetLayout(VkDevice device);

  // Data
  nvvk::DescriptorPack m_descPack{};

  core::VulkanBackend* m_backend = nullptr;

  std::shared_ptr<VulkanRenderResources> m_resources;
  std::unique_ptr<nvvk::GBuffer> m_gBuffers;
  std::unique_ptr<VulkanRaster> m_raster;
  std::unique_ptr<VulkanRayTracer> m_ray_tracer;
  std::unique_ptr<VulkanPostProcessor> m_post;
};
