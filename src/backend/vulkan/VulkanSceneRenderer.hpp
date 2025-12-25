#pragma once

#include <vulkan/vulkan.h>

#include <memory>

#include "backend/vulkan/VulkanContext.hpp"
#include "src/backend/ISceneRenderer.hpp"

class VulkanRaster;
class VulkanRayTracer;
class PostProcessor;
class IRenderBackend;
class VulkanSceneResources;
class CpuSceneResources;

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
  explicit VulkanSceneRenderer(std::shared_ptr<VulkanContext> ctx);
  ~VulkanSceneRenderer() override;

  // Delete copy/move
  VulkanSceneRenderer(const VulkanSceneRenderer&) = delete;
  VulkanSceneRenderer& operator=(const VulkanSceneRenderer&) = delete;
  VulkanSceneRenderer(VulkanSceneRenderer&&) = delete;
  VulkanSceneRenderer& operator=(VulkanSceneRenderer&&) = delete;

  // ---------------------------------------------------------------------------
  // Lifecycle
  // ---------------------------------------------------------------------------
  void init(CpuSceneResources& scene) override;
  void clear() override;
  void reload(bool use_raytracing) override;

  // ---------------------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------------------
  void render(VkCommandBuffer cmd, CameraPtr camera, const CpuSceneResources& scene, bool raytrace,
              shaderio::PushConstant& pushValues) const override;

  void post_process(VkCommandBuffer cmd);
  void onResize(VkCommandBuffer cmd, const VkExtent2D& size);

  // ---------------------------------------------------------------------------
  // Accessors
  // ---------------------------------------------------------------------------
  VkImage get_image(uint32_t index) const;

  VulkanRaster& raster() noexcept;
  const VulkanRaster& raster() const noexcept;

  VulkanRayTracer& ray_tracer() noexcept;
  const VulkanRayTracer& ray_tracer() const noexcept;

  PostProcessor& post_processor() noexcept;
  const PostProcessor& post_processor() const noexcept;

  const nvvk::GBuffer& gbuffers() const noexcept;
  std::shared_ptr<VulkanSceneResources> deviceResources() noexcept;

private:
  void init_gbuffers();

private:
  std::shared_ptr<VulkanContext> m_ctx = nullptr;

  // PIMPL: Using unique_ptr allows us to forward declare these types
  // instead of including their headers here.
  std::unique_ptr<nvvk::GBuffer> m_gBuffers;
  std::unique_ptr<VulkanRaster> m_raster;
  std::unique_ptr<VulkanRayTracer> m_ray_tracer;
  std::unique_ptr<PostProcessor> m_post;
  std::shared_ptr<SlangShaderCompiler> m_compiler;

  std::shared_ptr<VulkanSceneResources> m_resources;
};
