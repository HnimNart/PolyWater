#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include "core/Camera.hpp"

// Forward Declarations
class SceneResourcesManager;
class WindowSize;

namespace shaderio
{
struct GltfSceneInfo;
struct PushConstant;
}  // namespace shaderio
class IDeviceAssets;
class IToneMapper;

class ISceneRenderer
{
public:
  enum RenderOutput : uint8_t
  {
    Linear = 0,     // HDR, raw output
    ToneMapped = 1  // SDR, final output for presentation
  };

  virtual ~ISceneRenderer() = default;

  // -------------------------------------------------------------------------
  // Lifecycle
  // -------------------------------------------------------------------------
  virtual void init(const SceneResourcesManager& scene) = 0;
  virtual void deinit() = 0;
  virtual void onResize(const WindowSize& size) = 0;  // Make pure virtual to force implementation
  virtual void reload(bool useRaytracing) = 0;

  // -------------------------------------------------------------------------
  // Execution Cycle
  // -------------------------------------------------------------------------
  // Updates GPU buffers (lights, matrices). Returns pointer to the mapped info for debugging/GUI.
  virtual shaderio::GltfSceneInfo* updateSceneBuffers(SceneResourcesManager& scene) = 0;

  // Main render pass.
  virtual void render(CameraPtr camera, const SceneResourcesManager& scene, bool raytrace,
                      const shaderio::PushConstant& pushValues) const = 0;

  // Runs the post-processing pipeline (Tonemapping, Bloom, etc.)
  virtual void postProcess() = 0;

  // -------------------------------------------------------------------------
  // Accessors & Resources
  // -------------------------------------------------------------------------
  virtual std::shared_ptr<IDeviceAssets> deviceResources() noexcept = 0;
  virtual IToneMapper& postProcessor() noexcept = 0;

  // -------------------------------------------------------------------------
  // Output / IO
  // -------------------------------------------------------------------------
  virtual void* getImageDescriptor(RenderOutput output) const = 0;
  virtual void saveImage(const std::filesystem::path& filename, int quality = 100) const = 0;
};
