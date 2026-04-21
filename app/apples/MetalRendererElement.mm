#ifdef __APPLE__

#include "MetalRendererElement.hpp"

#include <imgui.h>

#include "app/Application.hpp"
#include "app/widgets/camera.hpp"
#include "backend/metal/core/MetalBackend.hpp"
#include "core/logger.hpp"
#include "core/path_utils.hpp"
#include "renderer/metal/MetalRenderer.hpp"
#include "scene/SceneLoader.hpp"

// ============================================================================
// Construction
// ============================================================================

/**********************************************************/
MetalRendererElement::MetalRendererElement(std::string sceneFile)
    : m_sceneFile(std::move(sceneFile))
/**********************************************************/
{}

// ============================================================================
// IAppElement – Lifecycle
// ============================================================================

/**********************************************************/
void MetalRendererElement::onAttach(app::Application *app)
/**********************************************************/
{
  m_app = app;

  auto *backend = dynamic_cast<MetalBackend *>(m_app->getBackend());
  assert(backend && "MetalRendererElement requires a MetalBackend");

  m_renderer = std::make_unique<MetalRenderer>(backend);
  m_sceneManager = SceneManager(m_renderer->deviceResources());

  if (!m_sceneFile.empty()) {
    loadScene(m_sceneFile);
    m_sceneFile.clear();
  }
}

/**********************************************************/
void MetalRendererElement::onDetach()
/**********************************************************/
{
  if (m_renderer) {
    m_renderer->deinit();
    m_renderer.reset();
  }
  m_sceneManager.clear();
}

/**********************************************************/
void MetalRendererElement::onResize(WindowSize /*size*/)
/**********************************************************/
{
  // MetalRenderer resizes lazily through beginFrame in MetalBackend.
}

// ============================================================================
// IAppElement – File handling
// ============================================================================

/**********************************************************/
void MetalRendererElement::onFileDrop(const std::filesystem::path &filename,
                                      glm::vec2 /*mousePos*/)
/**********************************************************/
{
  const std::string ext = filename.extension().string();
  if (ext == ".json") {
    m_sceneFile = filename.string();
  } else if (ext == ".obj" || ext == ".gltf" || ext == ".glb") {
    m_modelFileToLoad = filename.string();
  } else {
    LOGI("MetalRendererElement: unrecognised file type %s\n",
         filename.c_str());
  }
}

/**********************************************************/
void MetalRendererElement::onFileSelected(const std::filesystem::path &filename)
/**********************************************************/
{
  onFileDrop(filename, {});
}

// ============================================================================
// IAppElement – Frame loop
// ============================================================================

/**********************************************************/
void MetalRendererElement::onPreRender()
/**********************************************************/
{
  // --- Pending model load ---
  if (!m_modelFileToLoad.empty()) {
    auto &resourceMgr = m_sceneManager.sceneResourceManager();
    resourceMgr.loadModel(m_modelFileToLoad);
    resourceMgr.finalizeSceneResources();
    // Assets were already uploaded via m_device_resources; just rebuild graph.
    m_renderer->init(resourceMgr);
    m_modelFileToLoad.clear();
  }

  // --- Pending scene reload ---
  if (!m_sceneFile.empty()) {
    clear();
    loadScene(m_sceneFile);
    m_sceneFile.clear();
  }

  // Update camera matrices
  m_sceneManager.onPreRender();
}

/**********************************************************/
void MetalRendererElement::onRender(const IRenderContext &ctx)
/**********************************************************/
{
  if (!m_renderer || m_app->isPaused()) {
    return;
  }

  IRenderContext &mutableCtx = const_cast<IRenderContext &>(ctx);
  mutableCtx.sceneResources  = m_sceneManager.getScenePtr();

  m_renderer->render(mutableCtx);
}

// ============================================================================
// IAppElement – UI
// ============================================================================

/**********************************************************/
void MetalRendererElement::onUIRender()
/**********************************************************/
{
  auto camera = m_sceneManager.camera();

  if (ImGui::Begin("Settings")) {
    if (ImGui::BeginTabBar("MetalSettingsTabs")) {
      if (ImGui::BeginTabItem("Scene")) {
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
          app::cameraWidget(camera);
        }

        const Scene *scene = m_sceneManager.getScenePtr();
        if (scene) {
          ImGui::Separator();
          ImGui::Text("Meshes:    %u", (unsigned)scene->meshes.size());
          ImGui::Text("Instances: %u", (unsigned)scene->instances.size());
          ImGui::Text("Materials: %u", (unsigned)scene->materials.size());
        }
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }
  ImGui::End();
}

/**********************************************************/
void MetalRendererElement::onUIMenu()
/**********************************************************/
{
  // Nothing extra in the menu bar for the Metal renderer.
}

// ============================================================================
// Accessors
// ============================================================================

/**********************************************************/
CameraPtr MetalRendererElement::getCameraManipulator() const
/**********************************************************/
{
  return m_sceneManager.camera();
}

/**********************************************************/
const SceneManager &MetalRendererElement::getSceneManager() const
/**********************************************************/
{
  return m_sceneManager;
}

// ============================================================================
// Private helpers
// ============================================================================

/**********************************************************/
void MetalRendererElement::loadScene(const std::filesystem::path &filePath)
/**********************************************************/
{
  if (filePath.empty()) {
    LOGW("MetalRendererElement: loadScene called with empty path.\n");
    return;
  }

  auto filepath = core::findFile(filePath, common::getSceneDir());

  SceneLoader loader;
  SceneData sceneData;
  try {
    if (!loader.load(filepath.string(), sceneData)) {
      LOGE("MetalRendererElement: failed to parse scene '%s'\n",
           filepath.c_str());
      return;
    }
    LOGI("MetalRendererElement: loaded scene '%s'\n", filepath.c_str());
  } catch (const std::exception &e) {
    LOGE("MetalRendererElement: failed to load '%s': %s\n",
         filepath.c_str(), e.what());
    return;
  }

  m_sceneManager.buildSceneFromData(sceneData, common::getAssetDirs());
  m_sceneManager.sceneResourceManager().finalizeSceneResources();
  m_renderer->init(m_sceneManager.sceneResourceManager());
}

/**********************************************************/
void MetalRendererElement::clear()
/**********************************************************/
{
  if (m_renderer) {
    m_renderer->deinit();
  }
  m_sceneManager.clear();
}

#endif // __APPLE__
