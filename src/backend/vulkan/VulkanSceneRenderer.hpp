#pragma once

#include <vulkan/vulkan.h>

#include <memory>

#include "VulkanBackend.hpp"
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
  VkImage get_image(uint32_t index) const override;

  VulkanRaster& raster() noexcept override;
  const VulkanRaster& raster() const noexcept override;

  VulkanRayTracer& ray_tracer() noexcept override;
  const VulkanRayTracer& ray_tracer() const noexcept override;

  PostProcessor& post_processor() noexcept override;
  const PostProcessor& post_processor() const noexcept override;

  const nvvk::GBuffer& gbuffers() const noexcept;
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
  std::unique_ptr<PostProcessor> m_post;
  std::shared_ptr<SlangShaderCompiler> m_compiler;

  std::shared_ptr<VulkanSceneResources> m_resources;
};
