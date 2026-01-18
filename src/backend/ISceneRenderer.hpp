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
  virtual shaderio::GltfSceneInfo* updateSceneBuffers(SceneResourcesManager& scene) = 0;
  virtual void render(CameraPtr camera, const SceneResourcesManager& scene, bool raytrace,
                      shaderio::PushConstant& pushValues) const = 0;
  virtual void postProcess() = 0;
  virtual void reload(bool useRaytracing) = 0;
  virtual void onResize(const WindowSize& size) {};

  virtual std::shared_ptr<IDeviceResources> deviceResources() noexcept {};
  virtual IPostProcessor& postProcessor() noexcept = 0;

  // Output
  virtual void* getImageDescriptor(uint32_t index) const = 0;
  virtual void saveImage(const std::filesystem::path& filename, int quality = 100) const = 0;
};
