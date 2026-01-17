#pragma once

#include "IDeviceResources.hpp"
#include "core/Camera.hpp"
#include "core/Image.hpp"
#include "core/application/types.h"
#include "scene/SceneResources.hpp"
#include "shaders/post/IToneMapper.hpp"
#include "shaders/shaderio.h"

class ISceneRenderer
{
public:
  virtual ~ISceneRenderer() = default;
  virtual void init(const SceneResourcesManager& scene) = 0;
  virtual void deinit() = 0;
  virtual shaderio::GltfSceneInfo* update_buffers(CameraPtr camera,
                                                  SceneResourcesManager& scene) = 0;
  virtual void render(CameraPtr camera, const SceneResourcesManager& scene, bool raytrace,
                      shaderio::PushConstant& pushValues) const = 0;
  virtual void post_process() = 0;
  virtual void reload(bool use_raytracing) = 0;

  virtual std::shared_ptr<IDeviceResources> deviceResources() noexcept {};

  virtual IPostProcessor& post_processor() noexcept = 0;
  virtual void onResize(const WindowSize& size) {};
  virtual core::Image get_image(uint32_t index) const = 0;
};
