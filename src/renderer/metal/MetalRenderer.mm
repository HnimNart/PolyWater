#ifdef __APPLE__

#import "MetalRenderer.hpp"

#import "MetalDeviceAssets.hpp"
#import "passes/MetalRasterPass.hpp"

#import "backend/metal/core/MetalBackend.hpp"
#import "backend/metal/core/MetalContextManager.hpp"
#import "renderer/interfaces/IRenderGraph.hpp"
#import "scene/SceneResources.hpp"

// ---------------------------------------------------------------------------
// MetalRenderer
// ---------------------------------------------------------------------------

/**********************************************************/
MetalRenderer::MetalRenderer(MetalBackend *backend)
    : m_backend(backend), m_ctx(backend->getContextManager())
/**********************************************************/
{
  // Create the device-asset store immediately so that deviceResources()
  // returns a valid pointer before init() is called.  This mirrors the
  // Vulkan renderer where m_resources is allocated in the constructor.
  m_assets = std::make_shared<MetalDeviceAssets>(m_ctx);
}

/**********************************************************/
MetalRenderer::~MetalRenderer()
/**********************************************************/
{
  deinit();
}

/**********************************************************/
void MetalRenderer::init(const SceneResourcesManager & /*scene*/)
/**********************************************************/
{
  // Scene geometry has already been uploaded to m_assets by the
  // SceneResourcesManager via the IDeviceAssets interface
  // (upload / linkMeshToBuffer / uploadSceneResoures).
  // We only need to (re-)build the render graph here.
  buildGraph();
}

/**********************************************************/
void MetalRenderer::deinit()
/**********************************************************/
{
  if (m_graph) {
    m_graph->deinit();
    m_graph.reset();
  }
  if (m_assets) {
    // Clear GPU-side data only; keep the shared_ptr alive so the
    // SceneResourcesManager (which also holds m_assets) can re-upload
    // scene geometry after a reload without a dangling pointer.
    m_assets->deinit();
  }
}

/**********************************************************/
void MetalRenderer::buildGraph()
/**********************************************************/
{
  m_graph = std::make_unique<RenderGraph>("Metal-Raster");
  m_graph->addPass(
      std::make_unique<MetalRasterPass>(m_ctx, m_assets.get()));
  m_graph->init();
  m_graph->compile();
}

/**********************************************************/
bool MetalRenderer::update(const SceneResourcesManager &scene)
/**********************************************************/
{
  // No per-frame CPU updates needed for the basic raster path.
  return false;
}

/**********************************************************/
void MetalRenderer::reload()
/**********************************************************/
{
  // No hot-reload for Metal shaders in the initial implementation.
}

/**********************************************************/
void MetalRenderer::onResize(const WindowSize & /*size*/)
/**********************************************************/
{
  // Metal drawable is resized each frame in MetalBackend::beginFrame().
}

/**********************************************************/
void MetalRenderer::setRenderMode(const std::string & /*mode*/)
/**********************************************************/
{
  // Only "Raster" is supported.
}

/**********************************************************/
std::string MetalRenderer::getCurrentMode() const
/**********************************************************/
{
  return "Raster";
}

/**********************************************************/
std::vector<std::string> MetalRenderer::getAvaliableModes() const
/**********************************************************/
{
  return {"Raster"};
}

/**********************************************************/
void MetalRenderer::render(IRenderContext &ctx)
/**********************************************************/
{
  if (!m_graph) {
    return;
  }
  m_graph->execute(ctx);
}

/**********************************************************/
std::shared_ptr<IDeviceAssets> MetalRenderer::deviceResources() noexcept
/**********************************************************/
{
  return m_assets;
}

/**********************************************************/
IToneMapper &MetalRenderer::postProcessor() noexcept
/**********************************************************/
{
  return *this; // Self is the (no-op) tone-mapper stub.
}

/**********************************************************/
int64_t MetalRenderer::getImageDescriptor(RenderOutput /*output*/) const
/**********************************************************/
{
  // Metal renders directly to the swapchain; no off-screen descriptor.
  return 0;
}

/**********************************************************/
void MetalRenderer::saveImage(const std::filesystem::path & /*filename*/,
                              int /*quality*/) const
/**********************************************************/
{
  // Not yet implemented for Metal.
}

#endif // __APPLE__
