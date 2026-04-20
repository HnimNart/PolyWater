#ifdef __APPLE__

#import "MetalBackend.hpp"

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#import <GLFW/glfw3.h>
#import <GLFW/glfw3native.h>

#import "app/IGUISystem.hpp"
#import "backend/metal/gui/ImGuiMetalSystem.hpp"
#import "core/profiler.hpp"

// Objective-C++ implementation struct (hidden from C++ consumers).
struct MetalBackendData {
  CAMetalLayer                *metalLayer            = nil;
  id<MTLCommandBuffer>         currentCommandBuffer  = nil;
  MTLRenderPassDescriptor     *currentRenderPassDesc = nil;
  id<MTLRenderCommandEncoder>  currentRenderEncoder  = nil;
  id<CAMetalDrawable>          currentDrawable        = nil;
  id<MTLTexture>               depthTexture           = nil;
  CGSize                       depthTextureSize       = {0, 0};
};

/**********************************************************/
std::unique_ptr<MetalBackend>
MetalBackend::create(const app::ApplicationCreateInfo &appInfo)
/**********************************************************/
{
  auto backend = std::unique_ptr<MetalBackend>(new MetalBackend());
  if (!backend->initMetal(appInfo)) {
    return nullptr;
  }
  return backend;
}

/**********************************************************/
MetalBackend::MetalBackend() = default;
/**********************************************************/

/**********************************************************/
MetalBackend::~MetalBackend() = default;
/**********************************************************/

/**********************************************************/
bool MetalBackend::initMetal(const app::ApplicationCreateInfo &appInfo)
/**********************************************************/
{
  m_contextManager = std::make_unique<MetalContextManager>();
  if (!m_contextManager->init(appInfo)) {
    return false;
  }

  m_renderContext = std::make_unique<MetalRenderContext>();
  m_data          = std::make_unique<MetalBackendData>();
  return true;
}

/**********************************************************/
void MetalBackend::initPresentation(GLFWwindow *window, app::IGUISystemPtr gui)
/**********************************************************/
{
  m_windowHandle = window;

  if (m_windowHandle) {
    // Attach a CAMetalLayer to the GLFW Cocoa window's content view.
    NSWindow *nsWindow   = glfwGetCocoaWindow(m_windowHandle);
    NSView   *contentView = nsWindow.contentView;

    contentView.wantsLayer = YES;
    contentView.layerContentsRedrawPolicy =
        NSViewLayerContentsRedrawDuringViewResize;

    CAMetalLayer *metalLayer = [CAMetalLayer layer];
    metalLayer.device =
        (__bridge id<MTLDevice>)m_contextManager->getDeviceHandle();
    metalLayer.pixelFormat    = MTLPixelFormatBGRA8Unorm;
    metalLayer.framebufferOnly = YES;

    int w = 0, h = 0;
    glfwGetFramebufferSize(m_windowHandle, &w, &h);
    if (w > 0 && h > 0) {
      metalLayer.drawableSize = CGSizeMake(w, h);
    }
    contentView.layer    = metalLayer;
    m_data->metalLayer   = metalLayer;
  }

  if (!gui) {
    return;
  }

  auto metalGui = std::dynamic_pointer_cast<ImGuiMetalSystem>(gui);
  if (!metalGui) {
    throw std::runtime_error(
        "GUI system given to MetalBackend is not an ImGuiMetalSystem");
  }

  metalGui->initMetalBackend(*m_contextManager, m_windowHandle);
  m_renderRegistry.registerElement(metalGui);
}

/**********************************************************/
void MetalBackend::initProfiler(core::ProfilerTimeline * /*timeline*/)
/**********************************************************/
{
  // GPU profiling via Metal is achievable with MTLCounterSet or Instruments;
  // not implemented here for minimum requirements.
}

/**********************************************************/
void MetalBackend::deinit()
/**********************************************************/
{
  waitForDeviceIdle();

  m_data->depthTexture = nil;
  m_data->metalLayer = nil;

  if (m_contextManager) {
    m_contextManager->deinit();
  }
}

/**********************************************************/
IRenderContext &MetalBackend::getCurrentContext()
/**********************************************************/
{
  return *m_renderContext;
}

/**********************************************************/
IRenderContext *MetalBackend::beginFrame()
/**********************************************************/
{
  if (!m_data->metalLayer) {
    // Headless mode: return a context without a real drawable.
    return m_renderContext.get();
  }

  // Resize the Metal drawable to match the current framebuffer.
  if (m_windowHandle) {
    int w = 0, h = 0;
    glfwGetFramebufferSize(m_windowHandle, &w, &h);
    if (w > 0 && h > 0) {
      m_data->metalLayer.drawableSize = CGSizeMake(w, h);
    }
  }

  m_data->currentDrawable = [m_data->metalLayer nextDrawable];
  if (!m_data->currentDrawable) {
    return nullptr;
  }

  id<MTLCommandQueue> commandQueue =
      (__bridge id<MTLCommandQueue>)m_contextManager->getCommandQueueHandle();
  m_data->currentCommandBuffer = [commandQueue commandBuffer];

  // --- Depth texture: create or recreate if the size changed ---
  id<MTLDevice> device =
      (__bridge id<MTLDevice>)m_contextManager->getDeviceHandle();
  CGSize drawableSize = m_data->metalLayer.drawableSize;
  if (!m_data->depthTexture ||
      m_data->depthTextureSize.width  != drawableSize.width ||
      m_data->depthTextureSize.height != drawableSize.height) {
    MTLTextureDescriptor *depthDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
            MTLPixelFormatDepth32Float
                                                          width:(NSUInteger)drawableSize.width
                                                         height:(NSUInteger)drawableSize.height
                                                      mipmapped:NO];
    depthDesc.usage        = MTLTextureUsageRenderTarget;
    depthDesc.storageMode  = MTLStorageModePrivate;
    m_data->depthTexture      = [device newTextureWithDescriptor:depthDesc];
    m_data->depthTextureSize  = drawableSize;
  }

  // Build the render pass descriptor for this frame.
  m_data->currentRenderPassDesc = [MTLRenderPassDescriptor renderPassDescriptor];
  m_data->currentRenderPassDesc.colorAttachments[0].texture =
      m_data->currentDrawable.texture;
  m_data->currentRenderPassDesc.colorAttachments[0].loadAction =
      MTLLoadActionClear;
  m_data->currentRenderPassDesc.colorAttachments[0].clearColor =
      MTLClearColorMake(0.1, 0.1, 0.1, 1.0);
  m_data->currentRenderPassDesc.colorAttachments[0].storeAction =
      MTLStoreActionStore;
  m_data->currentRenderPassDesc.depthAttachment.texture     = m_data->depthTexture;
  m_data->currentRenderPassDesc.depthAttachment.loadAction  = MTLLoadActionClear;
  m_data->currentRenderPassDesc.depthAttachment.clearDepth  = 1.0;
  m_data->currentRenderPassDesc.depthAttachment.storeAction = MTLStoreActionDontCare;

  m_data->currentRenderEncoder = [m_data->currentCommandBuffer
      renderCommandEncoderWithDescriptor:m_data->currentRenderPassDesc];

  // Populate the shared render context with this frame's objects.
  m_renderContext->setFrameData(
      (__bridge void *)m_data->currentCommandBuffer,
      (__bridge void *)m_data->currentRenderPassDesc,
      (__bridge void *)m_data->currentRenderEncoder,
      (__bridge void *)m_data->currentDrawable);

  return m_renderContext.get();
}

/**********************************************************/
void MetalBackend::renderFrame(
    const std::vector<app::IAppElementPtr> &elements,
    IRenderContext const &ctx)
/**********************************************************/
{
  for (const auto &e : elements) {
    e->onRender(ctx);
  }

  for (const auto &e : elements) {
    e->onEndFrame(ctx);
  }

  // Draw registered renderables (e.g., ImGuiMetalSystem).
  for (const auto &renderable : m_renderRegistry.getElements()) {
    renderable->onRender(ctx);
  }
}

/**********************************************************/
void MetalBackend::endFrame(IRenderContext const & /*ctx*/)
/**********************************************************/
{
  if (m_data->currentRenderEncoder) {
    [m_data->currentRenderEncoder endEncoding];
    m_data->currentRenderEncoder = nil;
  }
}

/**********************************************************/
void MetalBackend::present()
/**********************************************************/
{
  if (m_data->currentCommandBuffer && m_data->currentDrawable) {
    [m_data->currentCommandBuffer
        presentDrawable:m_data->currentDrawable];
    [m_data->currentCommandBuffer commit];
    m_data->currentCommandBuffer = nil;
    m_data->currentDrawable      = nil;
  }
}

/**********************************************************/
void MetalBackend::advance()
/**********************************************************/
{
  // No ring-buffer index to advance for this simple Metal backend.
}

/**********************************************************/
void MetalBackend::waitForDeviceIdle()
/**********************************************************/
{
  if (m_contextManager) {
    m_contextManager->waitForIdle();
  }
}

/**********************************************************/
void MetalBackend::setVsync(bool enabled)
/**********************************************************/
{
  IRenderBackend::setVsync(enabled);
  if (m_data->metalLayer) {
    m_data->metalLayer.displaySyncEnabled = enabled ? YES : NO;
  }
}

/**********************************************************/
MetalContextManager *MetalBackend::getContextManager() const
/**********************************************************/
{
  return m_contextManager.get();
}

/**********************************************************/
RenderRegistry &MetalBackend::getRegistry()
/**********************************************************/
{
  return m_renderRegistry;
}

#endif // __APPLE__
