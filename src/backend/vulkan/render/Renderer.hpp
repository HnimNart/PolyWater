#pragma once

#include <vulkan/vulkan.h>

#include <memory>

#include "Acceleration.hpp"
#include "Raster.hpp"
#include "RayTracer.hpp"
#include "ToneMapper.hpp"
#include "backend/interfaces/IDeviceAssets.hpp"
#include "backend/interfaces/ISceneRenderer.hpp"
#include "backend/vulkan/core/Backend.hpp"
#include "scene/gltf/io_gltf.h"

class PostProcessor;
class IRenderBackend;
class VulkanSceneAssetManager;
class SceneResourcesManager;

namespace shaderio
{
struct PushConstant;
}

class VulkanRenderer final : public ISceneRenderer
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
  void reload(bool useRaytracing) override;

  // ---------------------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------------------
  shaderio::GltfSceneInfo* updateSceneBuffers(SceneResourcesManager& scene) override;
  void render(CameraPtr camera, const SceneResourcesManager& scene, bool raytrace,
              const shaderio::PushConstant& pushValues) const override;

  void postProcess() override;
  void onResize(const WindowSize& size) override;
  void saveImage(const std::filesystem::path& filename, int quality = 100) const override;

  // ---------------------------------------------------------------------------
  // Accessors
  // ---------------------------------------------------------------------------
  void* getImageDescriptor(ISceneRenderer::RenderOutput output) const override;
  IToneMapper& postProcessor() noexcept override;
  std::shared_ptr<IDeviceAssets> deviceResources() noexcept override;

private:
  void initGBuffers();
  shaderio::GltfSceneInfo* updateSceneBuffer(VkCommandBuffer cmd,
                                             SceneResourcesManager& scene) const;
  void createDescriptorSetLayout(VkDevice device);

  // Data
  nvvk::DescriptorPack m_descPack{};

  VulkanBackend* m_backend = nullptr;

  std::shared_ptr<VulkanSceneAssetManager> m_resources;
  std::unique_ptr<nvvk::GBuffer> m_gBuffers;
  std::unique_ptr<VulkanRaster> m_raster;
  std::unique_ptr<VulkanRayTracer> m_ray_tracer;
  std::unique_ptr<VulkanToneMapper> m_post;
};
