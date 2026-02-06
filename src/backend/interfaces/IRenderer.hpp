#pragma once

#include <filesystem>
#include <memory>

#include "backend/interfaces/IRenderContext.hpp"
#include "scene/SceneManager.hpp"

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

enum class RenderMode
{
  RAYTRACE = 0,
  RASTER = 1,
  COUNT = 2,
};

static inline const char* renderModeToString(RenderMode mode)
{
  switch (mode)
  {
    case RenderMode::RAYTRACE:
      return "Raytracing";
    case RenderMode::RASTER:
      return "Rasterization";
    default:
      return "Unknown";
  }
}

class IRenderer
{
public:
  virtual ~IRenderer() = default;

  // -------------------------------------------------------------------------
  // Lifecycle
  // -------------------------------------------------------------------------
  virtual void init(const SceneResourcesManager& scene) = 0;
  virtual void deinit() = 0;
  virtual void onResize(const WindowSize& size) = 0;
  virtual void reload(const SceneResourcesManager& scene) = 0;
  virtual void update(const SceneResourcesManager& scene) = 0;

  // -------------------------------------------------------------------------
  // Execution Cycle
  // -------------------------------------------------------------------------
  virtual void setRenderMode(RenderMode mode,
                             const SceneResourcesManager& scene) = 0;
  // Main render pass.
  virtual void render(IRenderContext& ctx) const = 0;

  // -------------------------------------------------------------------------
  // Accessors & Resources
  // -------------------------------------------------------------------------
  virtual std::shared_ptr<IDeviceAssets> deviceResources() noexcept = 0;
  virtual IToneMapper& postProcessor() noexcept = 0;

  // -------------------------------------------------------------------------
  // Output / IO
  // -------------------------------------------------------------------------
  virtual void* getImageDescriptor(RenderOutput output) const = 0;
  virtual void saveImage(const std::filesystem::path& filename,
                         int quality = 100) const = 0;

protected:
  RenderMode m_render_mode = RenderMode::RAYTRACE;
};
