#ifdef __APPLE__

#import "metal_imgui_system.hpp"

#import <GLFW/glfw3.h>
#import <backends/imgui_impl_glfw.h>
#import <backends/imgui_impl_metal.h>
#import <imgui.h>
#import <imgui_internal.h>

#import <Metal/Metal.h>

#import <app/widgets/fonts.hpp>
#import <app/widgets/style.hpp>
#import <core/file_operations.hpp>

#import "app/app_info.hpp"
#import "backend/metal/core/metal_context_manager.hpp"
#import "backend/metal/core/metal_render_context.hpp"

/**********************************************************/
MetalImGuiSystem::~MetalImGuiSystem()
/**********************************************************/
{
  deinit();
}

/**********************************************************/
void MetalImGuiSystem::init(const app::ApplicationCreateInfo &info)
/**********************************************************/
{
  if (m_contextCreated) {
    return;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  m_contextCreated = true;

  setupImGui(info);
}

/**********************************************************/
void MetalImGuiSystem::deinit()
/**********************************************************/
{
  saveSettings(m_iniFilename.c_str());
  shutdownMetalBackend();
  destroyContext();
}

/**********************************************************/
void MetalImGuiSystem::destroyContext()
/**********************************************************/
{
  if (!m_contextCreated) {
    return;
  }

  ImGui::DestroyContext();
  m_contextCreated = false;
}

/******************************************************************************
 * ImGui Setup
 *****************************************************************************/

/**********************************************************/
void MetalImGuiSystem::setupImGui(const app::ApplicationCreateInfo &info)
/**********************************************************/
{
  m_iniFilename =
      core::utf8FromPath(core::getExecutablePath().replace_extension(".ini"));

  ImGui::LoadIniSettingsFromDisk(m_iniFilename.c_str());
  app::Style{}.setStyle(false);

  configureImGuiIO(info);
  initializeFonts();
  loadSettings(m_iniFilename.c_str());
}

/**********************************************************/
void MetalImGuiSystem::configureImGuiIO(
    const app::ApplicationCreateInfo &info)
/**********************************************************/
{
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

  // Multi-viewport requires platform-specific window creation beyond what is
  // implemented here; disable it for the Metal backend.
  io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;

  io.IniFilename = m_iniFilename.c_str();
}

/**********************************************************/
void MetalImGuiSystem::initializeFonts()
/**********************************************************/
{
  ImGuiIO &io = ImGui::GetIO();

  app::addDefaultFont();
  io.FontDefault = app::getDefaultFont();
  app::addMonospaceFont();
}

/******************************************************************************
 * Metal Backend Initialization
 *****************************************************************************/

/**********************************************************/
void MetalImGuiSystem::initMetalBackend(MetalContextManager &contextManager,
                                        GLFWwindow *windowHandle)
/**********************************************************/
{
  if (m_metalInitialized) {
    return;
  }

  initializeGlfwBackend(windowHandle);
  initializeMetalBackend(contextManager);

  m_metalInitialized = true;
}

/**********************************************************/
void MetalImGuiSystem::initializeGlfwBackend(GLFWwindow *windowHandle)
/**********************************************************/
{
  if (windowHandle) {
    // Use InitForOther since Metal is not a Vulkan/OpenGL backend.
    ImGui_ImplGlfw_InitForOther(windowHandle, true);
    m_glfwInitialized = true;
  }
}

/**********************************************************/
void MetalImGuiSystem::initializeMetalBackend(
    MetalContextManager &contextManager)
/**********************************************************/
{
  id<MTLDevice> device =
      (__bridge id<MTLDevice>)contextManager.getDeviceHandle();
  ImGui_ImplMetal_Init(device);
}

/**********************************************************/
void MetalImGuiSystem::shutdownMetalBackend()
/**********************************************************/
{
  if (!m_metalInitialized) {
    return;
  }

  ImGui_ImplMetal_Shutdown();

  if (m_glfwInitialized) {
    ImGui_ImplGlfw_Shutdown();
  }

  m_metalInitialized = false;
}

/******************************************************************************
 * Frame Operations
 *****************************************************************************/

/**********************************************************/
void MetalImGuiSystem::beginFrame()
/**********************************************************/
{
  if (m_glfwInitialized) {
    ImGui_ImplGlfw_NewFrame();
  }
  ImGui::NewFrame();
}

/**********************************************************/
void MetalImGuiSystem::endFrame()
/**********************************************************/
{
  ImGui::EndFrame();
}

/**********************************************************/
void MetalImGuiSystem::render()
/**********************************************************/
{
  ImGui::Render();
}

/**********************************************************/
void MetalImGuiSystem::onRender(const IRenderContext &ctx)
/**********************************************************/
{
  if (!m_metalInitialized) {
    return;
  }

  const MetalRenderContext &metalCtx = MetalRenderContext::get(ctx);

  id<MTLCommandBuffer> commandBuffer =
      (__bridge id<MTLCommandBuffer>)metalCtx.getCommandBufferHandle();
  id<MTLRenderCommandEncoder> encoder =
      (__bridge id<MTLRenderCommandEncoder>)metalCtx.getRenderCommandEncoderHandle();
  MTLRenderPassDescriptor *rpd =
      (__bridge MTLRenderPassDescriptor *)metalCtx.getRenderPassDescriptorHandle();

  if (!commandBuffer || !encoder || !rpd) {
    return;
  }

  // ImGui_ImplMetal_NewFrame must be called with a render pass descriptor that
  // has a real color attachment texture so the backend can determine the pixel
  // format and (re)create the render pipeline state if needed.
  // We call it here (inside onRender) rather than in beginFrame() because the
  // drawable – and therefore the descriptor – is only available after
  // MetalBackend::beginFrame() has acquired it.
  ImGui_ImplMetal_NewFrame(rpd);

  ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, encoder);
}

/******************************************************************************
 * Menu & Docking
 *****************************************************************************/

/**********************************************************/
void MetalImGuiSystem::renderMenu(
    const std::vector<std::shared_ptr<app::IAppElement>> &elements)
/**********************************************************/
{
  setupImguiDock();

  if (ImGui::BeginMainMenuBar()) {
    for (const auto &element : elements) {
      element->callOnUIMenu();
    }
    ImGui::EndMainMenuBar();
  }
}

/**********************************************************/
void MetalImGuiSystem::setupImguiDock()
/**********************************************************/
{
  const ImGuiDockNodeFlags dockFlags =
      ImGuiDockNodeFlags_PassthruCentralNode |
      ImGuiDockNodeFlags_NoDockingInCentralNode;

  ImGuiID dockID =
      ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockFlags);

  if (!ImGui::DockBuilderGetNode(dockID)->IsSplitNode() &&
      !ImGui::FindWindowByName("Viewport")) {
    setupDefaultDockLayout(dockID);
  }
}

/**********************************************************/
void MetalImGuiSystem::setupDefaultDockLayout(ImGuiID dockID)
/**********************************************************/
{
  ImGui::DockBuilderDockWindow("Viewport", dockID);
  ImGui::DockBuilderGetCentralNode(dockID)->LocalFlags |=
      ImGuiDockNodeFlags_NoTabBar;

  if (m_dockSetup) {
    m_dockSetup(dockID);
  } else {
    createDefaultLayout(dockID);
  }
}

/**********************************************************/
void MetalImGuiSystem::createDefaultLayout(ImGuiID dockID)
/**********************************************************/
{
  ImGuiID leftID = ImGui::DockBuilderSplitNode(dockID, ImGuiDir_Left, 0.2f,
                                                nullptr, &dockID);
  ImGui::DockBuilderDockWindow("Settings", leftID);
}

/**********************************************************/
void MetalImGuiSystem::setDockSetup(std::function<void(ImGuiID)> fn)
/**********************************************************/
{
  m_dockSetup = std::move(fn);
}

/**********************************************************/
void MetalImGuiSystem::onDpiScaleChanged(float scaleRatio)
/**********************************************************/
{
  if (m_contextCreated) {
    ImGui::GetIO().FontGlobalScale *= scaleRatio;
  }
}

/******************************************************************************
 * Window Queries & Configuration
 *****************************************************************************/

/**********************************************************/
bool MetalImGuiSystem::getWindowSize(const std::string &windowName,
                                     WindowSize &size)
/**********************************************************/
{
  const ImGuiWindow *viewport = ImGui::FindWindowByName(windowName.c_str());
  if (!viewport) {
    return false;
  }

  size = {uint32_t(viewport->Size.x), uint32_t(viewport->Size.y)};
  return true;
}

/**********************************************************/
void MetalImGuiSystem::setWindowSize(const WindowSize &size)
/**********************************************************/
{
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize.x = float(size.width);
  io.DisplaySize.y = float(size.height);
}

/**********************************************************/
void MetalImGuiSystem::setConfigFlags(unsigned int flags)
/**********************************************************/
{
  if (m_contextCreated) {
    ImGui::GetIO().ConfigFlags |= flags;
  }
}

/**********************************************************/
void MetalImGuiSystem::loadSettings(const char *filename)
/**********************************************************/
{
  if (m_contextCreated) {
    ImGui::LoadIniSettingsFromDisk(filename);
  }
}

/**********************************************************/
void MetalImGuiSystem::saveSettings(const char *filename)
/**********************************************************/
{
  if (m_contextCreated) {
    ImGui::SaveIniSettingsToDisk(filename);
  }
}

#endif // __APPLE__
