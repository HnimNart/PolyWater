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
  void reload(bool use_raytracing) override;

  // ---------------------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------------------
  shaderio::GltfSceneInfo* update_buffers(CameraPtr camera, SceneResourcesManager& scene) override;
  void render(CameraPtr camera, const SceneResourcesManager& scene, bool raytrace,
              shaderio::PushConstant& pushValues) const override;

  void post_process() override;
  void onResize(const WindowSize& size) override;

  // ---------------------------------------------------------------------------
  // Accessors
  // ---------------------------------------------------------------------------
  core::Image get_image(uint32_t index) const override;
  IPostProcessor& post_processor() noexcept override;
  std::shared_ptr<IDeviceResources> deviceResources() noexcept override;

private:
  void init_gbuffers();
  shaderio::GltfSceneInfo* updateSceneBuffer(VkCommandBuffer cmd, CameraPtr camera,
                                             SceneResourcesManager& scene) const;

private:
  core::VulkanBackend* m_backend = nullptr;

  // PIMPL: Using unique_ptr allows us to forward declare these types
  // instead of including their headers here.
  std::unique_ptr<nvvk::GBuffer> m_gBuffers;
  std::unique_ptr<VulkanRaster> m_raster;
  std::unique_ptr<VulkanRayTracer> m_ray_tracer;
  std::unique_ptr<VulkanPostProcessor> m_post;
  std::shared_ptr<VulkanRenderResources> m_resources;
};
