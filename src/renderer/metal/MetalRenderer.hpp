#pragma once

#ifdef __APPLE__

#include <memory>
#include <string>
#include <vector>

#include "renderer/interfaces/IToneMapper.hpp"
#include "renderer/interfaces/IRenderer.hpp"

class MetalBackend;
class MetalContextManager;
class MetalDeviceAssets;
class RenderGraph;
struct WindowSize;

//------------------------------------------------------------
// MetalRenderer
//------------------------------------------------------------
// Concrete Metal implementation of IRenderer.
// Manages a MetalDeviceAssets instance for geometry upload and a
// RenderGraph that currently contains a single MetalRasterPass.
//
// Rendering goes directly to the swapchain – there is no off-screen
// GBuffer or tone-mapping stage for the initial rasterisation path.
class MetalRenderer final : public IRenderer, public IToneMapper {
public:
  explicit MetalRenderer(MetalBackend *backend);
  ~MetalRenderer() override;

  // -------------------------------------------------------------------------
  // IRenderer – Lifecycle
  // -------------------------------------------------------------------------
  void init(const SceneResourcesManager &scene) override;
  void deinit() override;
  void onResize(const WindowSize &size) override;
  void reload() override;
  bool update(const SceneResourcesManager &scene) override;

  // -------------------------------------------------------------------------
  // IRenderer – Rendering
  // -------------------------------------------------------------------------
  void setRenderMode(const std::string &mode) override;
  std::string getCurrentMode() const override;
  std::vector<std::string> getAvaliableModes() const override;
  void render(IRenderContext &ctx) override;

  // -------------------------------------------------------------------------
  // IRenderer – Accessors
  // -------------------------------------------------------------------------
  std::shared_ptr<IDeviceAssets> deviceResources() noexcept override;
  IToneMapper &postProcessor() noexcept override;
  int64_t getImageDescriptor(RenderOutput output) const override;
  void saveImage(const std::filesystem::path &filename,
                 int quality = 100) const override;

private:
  void buildGraph();
  void uploadScene(const SceneResourcesManager &scene);

  MetalBackend        *m_backend;
  MetalContextManager *m_ctx;

  std::shared_ptr<MetalDeviceAssets> m_assets;
  std::unique_ptr<RenderGraph>        m_graph;
};

#endif // __APPLE__
