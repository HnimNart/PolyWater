#pragma once

#include "core/camera.hpp"
#include "scene/scene_resources.hpp"
#include "shaders/shaderio.h"

class IBackend
{
  virtual void init(CpuSceneResources& scene) = 0;
  virtual void clear() = 0;
  virtual void render(VkCommandBuffer cmd, CameraPtr camera, const CpuSceneResources& scene,
                      bool raytrace, shaderio::PushConstant& pushValues) const = 0;
  virtual void reload(bool use_raytracing) = 0;
};