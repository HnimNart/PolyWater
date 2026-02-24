#pragma once

#include <filesystem>
#include <memory>

#include "backend/interfaces/IRenderContext.hpp"
#include "renderer/ShaderManager.hpp"
#include "scene/SceneManager.hpp"

// Forward Declarations
class SceneResourcesManager;
struct WindowSize;

namespace shaderio {
struct SceneInfo;
struct PushConstant;
struct RenderParams;
} // namespace shaderio
class IDeviceAssets;
class IToneMapper;

enum class RenderMode {
  RAYTRACE = 0,
  RASTER = 1,
  MESHLET = 2,
  COUNT = 3,
};

static inline const char *renderModeToString(RenderMode mode) {
  switch (mode) {
  case RenderMode::RAYTRACE:
    return "Raytracing";
  case RenderMode::RASTER:
    return "Rasterization";
  case RenderMode::MESHLET:
    return "Meshlet";
  default:
    return "Unknown";
  }
}

class IRenderer {
public:
  virtual ~IRenderer() = default;

  // -------------------------------------------------------------------------
  // Lifecycle
  // -------------------------------------------------------------------------
  virtual void init(const SceneResourcesManager &scene) = 0;
  virtual void deinit() = 0;
  virtual void onResize(const WindowSize &size) = 0;
  virtual void reload() = 0;
  virtual bool update(const SceneResourcesManager &scene) = 0;
  virtual void reset() { m_frameIndex = 0; };

  // -------------------------------------------------------------------------
  // Execution Cycle
  // -------------------------------------------------------------------------
  virtual void setRenderMode(RenderMode mode) = 0;
  // Main render pass.
  virtual void render(IRenderContext &ctx) = 0;

  // -------------------------------------------------------------------------
  // Accessors & Resources
  // -------------------------------------------------------------------------
  virtual std::shared_ptr<IDeviceAssets> deviceResources() noexcept = 0;
  virtual IToneMapper &postProcessor() noexcept = 0;
  shaderio::RenderParams &renderParams() { return m_renderParams; }
  shaderio::RasterParams &rasterParams() { return m_rasterParams; }
  RenderMode getRenderMode() const { return m_render_mode; }
  const ShaderManager &getShaderManager() const { return m_shaderManager; }
  uint32_t getFrameCount() const { return m_frameIndex; }

  // -------------------------------------------------------------------------
  // Output / IO
  // -------------------------------------------------------------------------
  virtual int64_t getImageDescriptor(RenderOutput output) const = 0;
  virtual void saveImage(const std::filesystem::path &filename,
                         int quality = 100) const = 0;

protected:
  RenderMode m_render_mode = RenderMode::RAYTRACE;
  shaderio::RenderParams m_renderParams;
  shaderio::RasterParams m_rasterParams;
  ShaderManager m_shaderManager;
  uint32_t m_frameIndex = 0;
};
