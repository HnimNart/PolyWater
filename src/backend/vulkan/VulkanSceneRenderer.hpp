#pragma once

#include <vulkan/vulkan.h>

#include <memory>

#include "VulkanAcceleration.hpp"
#include "VulkanBackend.hpp"
#include "VulkanPostToneMapper.hpp"
#include "VulkanRaster.hpp"
#include "VulkanRayTracer.hpp"
#include "src/backend/ISceneRenderer.hpp"

class PostProcessor;
class IRenderBackend;
class VulkanSceneResources;
class SceneResources;

namespace nvvk
{
class GBuffer;
}
namespace shaderio
{
struct PushConstant;
}

class VulkanSceneRenderer final : public ISceneRenderer
{
public:
  explicit VulkanSceneRenderer(core::VulkanBackend* backend);
  ~VulkanSceneRenderer() override;

  // Delete copy/move
  VulkanSceneRenderer(const VulkanSceneRenderer&) = delete;
  VulkanSceneRenderer& operator=(const VulkanSceneRenderer&) = delete;
  VulkanSceneRenderer(VulkanSceneRenderer&&) = delete;
  VulkanSceneRenderer& operator=(VulkanSceneRenderer&&) = delete;

  // ---------------------------------------------------------------------------
  // Lifecycle
  // ---------------------------------------------------------------------------
  void init(SceneResources& scene) override;
  void deinit() override;
  void reload(bool use_raytracing) override;

  // ---------------------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------------------

  void render(CameraPtr camera, SceneResources& scene, bool raytrace,
              shaderio::PushConstant& pushValues) const override;

  void post_process() override;
  void onResize(const WindowSize& size) override;

  // ---------------------------------------------------------------------------
  // Accessors
  // ---------------------------------------------------------------------------
  core::Image get_image(uint32_t index) const override;
  IPostProcessor& post_processor() noexcept override;

  std::shared_ptr<VulkanSceneResources> deviceResources() noexcept override;

private:
  void init_gbuffers();
  void updateSceneBuffer(VkCommandBuffer cmd, CameraPtr camera, SceneResources& scene) const;

private:
  core::VulkanBackend* m_backend = nullptr;

  // PIMPL: Using unique_ptr allows us to forward declare these types
  // instead of including their headers here.
  std::unique_ptr<nvvk::GBuffer> m_gBuffers;
  std::unique_ptr<VulkanRaster> m_raster;
  std::unique_ptr<VulkanRayTracer> m_ray_tracer;
  std::unique_ptr<VulkanPostProcessor> m_post;
  std::shared_ptr<VulkanSceneResources> m_resources;
  std::shared_ptr<SlangShaderCompiler> m_compiler;
};
