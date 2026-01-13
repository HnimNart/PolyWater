#pragma once

#include "backend/vulkan/VulkanRaster.hpp"
#include "backend/vulkan/VulkanRayTracer.hpp"
#include "core/Camera.hpp"
#include "scene/SceneResources.hpp"
#include "shaders/post/tonemapper.hpp"
#include "shaders/shaderio.h"

// TODO make this vulkan indepenednt
class ISceneRenderer
{
public:
  virtual ~ISceneRenderer() = default;
  virtual void init(SceneResources& scene) = 0;
  virtual void deinit() = 0;
  virtual void render(VkCommandBuffer cmd, CameraPtr camera, const SceneResources& scene,
                      bool raytrace, shaderio::PushConstant& pushValues) const = 0;
  virtual void reload(bool use_raytracing) = 0;

  virtual std::shared_ptr<VulkanSceneResources> deviceResources() noexcept {};

  virtual VulkanRaster& raster() noexcept {};
  virtual const VulkanRaster& raster() const noexcept {};

  virtual VulkanRayTracer& ray_tracer() noexcept {};
  virtual const VulkanRayTracer& ray_tracer() const noexcept {};

  virtual PostProcessor& post_processor() noexcept {};
  virtual const PostProcessor& post_processor() const noexcept {};

  virtual void post_process(VkCommandBuffer cmd) {};
  virtual void onResize(VkCommandBuffer cmd, const VkExtent2D& size) {};
  virtual VkImage get_image(uint32_t index) const { return {}; };
};
