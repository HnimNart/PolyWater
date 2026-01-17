#pragma once

#include "core/Camera.hpp"
#include "core/Image.hpp"
#include "scene/SceneResources.hpp"
#include "shaders/post/IToneMapper.hpp"
#include "shaders/shaderio.h"

// TODO make this vulkan indepenednt
class ISceneRenderer
{
public:
  virtual ~ISceneRenderer() = default;
  virtual void init(SceneResources& scene) = 0;
  virtual void deinit() = 0;
  virtual void render(CameraPtr camera, SceneResources& scene, bool raytrace,
                      shaderio::PushConstant& pushValues) const = 0;
  virtual void post_process() = 0;
  virtual void reload(bool use_raytracing) = 0;

  virtual std::shared_ptr<VulkanSceneResources> deviceResources() noexcept {};

  virtual IPostProcessor& post_processor() noexcept = 0;
  virtual void onResize(const WindowSize& size) {};
  virtual core::Image get_image(uint32_t index) const = 0;
};
