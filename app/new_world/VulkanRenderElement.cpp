#include "VulkanRenderElement.hpp"

// Standard Libs
#include <cstdio>
#include <fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <random>

// Third Party
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>
#include <imgui/imgui.h>
#include <shaders/shared/structs.h>
#include <vulkan/vulkan.h>

#include "app/Application.hpp"
#include "app/widgets/axis.hpp"
#include "app/widgets/camera.hpp"
#include "app/widgets/instance_editor.hpp"
#include "app/widgets/light_editor.hpp"
#include "app/widgets/material_editor.hpp"
#include "app/widgets/meshes_editor.hpp"
#include "app/widgets/render_editor.hpp"
#include "app/widgets/tonemapper.hpp"
#include "backend/vulkan/core/Backend.hpp"
#include "core/Image.hpp"
#include "core/path_utils.hpp"
#include "core/string_utils.h"
#include "core/timers.hpp"
#include "renderer/interfaces/IToneMapper.hpp"
#include "renderer/vulkan/Renderer.hpp"
#include "scene/SceneLoader.hpp"
#include "widgets/tooltip.hpp"

// ============================================================================
// 1. Lifecycle & System Initialization
// ============================================================================

/**********************************************************/
void VulkanRendererElement::onAttach(app::Application *app)
/**********************************************************/
{
  m_app = app;
  auto *backend = dynamic_cast<VulkanBackend *>(m_app->getBackend());
  assert(backend && "Backend is not VulkanBackend");
  m_renderer =
      std::make_unique<VulkanRenderer>(backend, common::getShaderDirs());
  m_sceneManager = SceneManager(m_renderer->deviceResources());
  loadScene(m_sceneFile);
}

/**********************************************************/
void VulkanRendererElement::onDetach()
/**********************************************************/
{
  if (m_renderer) {
    m_renderer->deinit();
  }
  m_sceneManager.clear();
}

/**********************************************************/
void VulkanRendererElement::clear()
/**********************************************************/
{
  m_renderer->clear();
  m_sceneManager.clear();
}

/**********************************************************/
void VulkanRendererElement::onResize(WindowSize size)
/**********************************************************/
{
  m_renderer->onResize(size);
}

// ============================================================================
// 2. Scene Management & Loading
// ============================================================================

/**********************************************************/
void VulkanRendererElement::onFileDrop(const std::filesystem::path &filename)
/**********************************************************/
{
  std::string ext = filename.extension().string();
  core::toLower(ext);
  if (ext == ".json") {
    LOGI("Scene File dropped: %s\n", filename.c_str());
    m_sceneFile = filename;
  } else if (ext == ".obj" || ext == ".gltf" || ext == ".glb") {
    LOGI("Model File dropped: %s\n", filename.c_str());
    m_modelFileToLoad = filename;
  } else {
    LOGI("Error: Dropped file is not a recognised file ( %s )\n",
         filename.c_str());
  }
}

/**********************************************************/
void VulkanRendererElement::loadScene(const std::filesystem::path &filePath)
/**********************************************************/
{
  if (filePath.empty()) {
    throw std::runtime_error("Empty filepath given to LoadScene()");
  }
  SCOPED_TIMER_FUNC();
  SceneLoader loader;
  SceneData sceneData;
  auto filepath = core::findFile(filePath, common::getSceneDir());
  if (!loader.load(filepath, sceneData)) {
    return;
  }

  m_sceneManager.buildSceneFromData(sceneData, common::getAssetDirs());

  // Procedural "Spiral" Generation for default scene
  if (filePath.filename() == "default_scene.json") {
    SceneResourcesManager &scene_resources =
        m_sceneManager.sceneResourceManager();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> disScale(0.1f, 1.0f);

    const int numInstances = 400;
    const float goldenAngle = 2.39996323f;
    const float spread = 0.8f;
    const float centerOpenRadius = 5.0f;

    for (int i = 0; i < numInstances; ++i) {
      float t = (float)i;
      float r = centerOpenRadius + (spread * std::sqrt(t));
      float theta = t * goldenAngle;

      float x = r * std::cos(theta);
      float z = r * std::sin(theta);
      float y = std::sin(r * 0.5f) * 2.0f + 2.0f;

      glm::vec3 pos = glm::vec3(x, y, z);
      float hue = std::fmod(theta * 0.1f, 1.0f);
      glm::vec3 color =
          glm::vec3(0.5f + 0.5f * std::cos(6.28f * (hue + 0.00f)),
                    0.5f + 0.5f * std::cos(6.28f * (hue + 0.33f)),
                    0.5f + 0.5f * std::cos(6.28f * (hue + 0.67f)));

      std::string baseObject = "Sphere";
      std::string name = baseObject + std::to_string(i);
      MaterialID matId = scene_resources.addMaterial(
          {.baseColorFactor = glm::vec4(color, 1.0f),
           .ior = glm::vec3(1.3f),
           .metallicFactor = (i % 2 == 0) ? 1.0f : 1e-4f,
           .roughnessFactor = 0.1f + (r / 30.0f),
           .sigma_t = glm::vec3((r / 20.0f) * 10.0f)},
          name);

      scene_resources.addInstance(
          {.translation = pos,
           .scale = glm::vec3(disScale(gen) * 0.8f),
           .materialIndex = matId,
           .meshIndex = scene_resources.getMeshIDFromName(baseObject),
           .hit_group = MaterialType::eVolumetric},
          name);
    }
  }

  m_sceneManager.sceneResourceManager().finalizeSceneResources();
  m_renderer->init(m_sceneManager.sceneResourceManager());
  m_sceneFile.clear();
}

// ============================================================================
// 3. Frame Loop Callbacks
// ============================================================================

/**********************************************************/
void VulkanRendererElement::onPreRender()
/**********************************************************/
{
  if (!m_sceneFile.empty()) {
    clear();
    loadScene(m_sceneFile);
  }

  if (!m_modelFileToLoad.empty()) {
    m_sceneManager.sceneResourceManager().loadModel(m_modelFileToLoad);
    m_sceneManager.sceneResourceManager().finalizeSceneResources();
    m_modelFileToLoad.clear();
  }

  m_hasChanged |= m_sceneManager.camera()->isDirty();
  if (m_hasChanged) {
    m_sceneManager.camera()->setClean();
  }
  m_hasChanged |= m_renderer->update(m_sceneManager.sceneResourceManager());
  if (m_hasChanged) {
    m_renderer->reset();
  }
  m_sceneManager.onPreRender();
}

/**********************************************************/
void VulkanRendererElement::onRender(const IRenderContext &ctx)
/**********************************************************/
{
  if (!m_renderer || m_app->isPaused()) {
    return;
  }

  shaderio::PushConstant pushValues{};
  IRenderContext &ctx_ref = const_cast<IRenderContext &>(ctx);
  ctx_ref.pushValues = pushValues;
  ctx_ref.sceneResources = m_sceneManager.getScenePtr();

  m_renderer->render(ctx_ref);
}

/**********************************************************/
void VulkanRendererElement::onEndFrame(const IRenderContext &)
/**********************************************************/
{}

/**********************************************************/
void VulkanRendererElement::onLastHeadlessFrame()
/**********************************************************/
{
  m_renderer->saveImage(
      core::getExecutablePath().replace_extension(".jpg").string());
}

// ============================================================================
// 4. User Interface (ImGui)
// ============================================================================

/**********************************************************/
void VulkanRendererElement::onUIRender()
/**********************************************************/
{
  namespace PE = app::PropertyEditor;
  m_hasChanged = false;

  // --- Shared Variable Extractions ---
  auto &resourceManager = m_sceneManager.sceneResourceManager();
  std::shared_ptr<core::CameraManipulator> camera = m_sceneManager.camera();
  auto *renderer = m_renderer.get();

  // --- VIEWPORT WINDOW ---
  if (ImGui::Begin("Viewport") && !m_app->isPaused()) {
    ImTextureID toneMappedId =
        ImTextureID(renderer->getImageDescriptor(RenderOutput::ToneMapped));
    ImGui::Image(toneMappedId, ImGui::GetContentRegionAvail());
    app::drawAxis(camera->getViewProjection());
  }
  ImGui::End();

  // --- SETTINGS WINDOW ---
  if (ImGui::Begin("Settings")) {
    if (ImGui::BeginTabBar("SettingTabs")) {

      // --- RENDERING ---
      if (ImGui::BeginTabItem("Render")) {
        if (ImGui::CollapsingHeader("Tonemapper",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
          core::tonemapperWidget(renderer->postProcessor().data());
        }

        if (ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen)) {
          m_hasChanged |= app::renderEditor(resourceManager, renderer);
        }
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }
  ImGui::End();

  // --- SCENE WINDOW ---
  if (ImGui::Begin("Scene")) {
    if (ImGui::BeginTabBar("SceneTabBar")) {

      // --- GLOBAL (Camera & Lighting) ---
      if (ImGui::BeginTabItem("Global")) {
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
          m_hasChanged |= app::cameraWidget(camera);
        }
        if (ImGui::CollapsingHeader("Environment",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
          m_hasChanged |= app::lightEditor(resourceManager);
        }
        ImGui::EndTabItem();
      }

      // --- MATERIALS ---
      if (ImGui::BeginTabItem("Materials")) {
        if (app::materialEditor(resourceManager)) {
          resourceManager.onMaterialChange();
        }
        ImGui::EndTabItem();
      }

      // --- INSTANCES ---
      if (ImGui::BeginTabItem("Instances")) {
        if (app::instanceEditor(resourceManager,
                                renderer->getShaderManager().getRegistry())) {
          resourceManager.onInstanceChange();
        }
        ImGui::EndTabItem();
      }

      // --- MESHES ---
      if (ImGui::BeginTabItem("Meshes")) {
        app::meshEditor(resourceManager);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Textures")) {
        const auto &textureMap = resourceManager.textureImageMap();

        static char filter[128] = "";
        ImGui::InputTextWithHint("##Filter", "Filter textures...", filter,
                                 IM_ARRAYSIZE(filter));
        ImGui::Separator();

        std::string searchStr = filter;
        std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(),
                       ::tolower);

        for (const auto &[name, image] : textureMap) {
          if (!image.isValid())
            continue;

          // Filtering by name or filename
          std::string nameLower = name;
          std::string fileLower = image.filename;
          std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                         ::tolower);
          std::transform(fileLower.begin(), fileLower.end(), fileLower.begin(),
                         ::tolower);

          if (!searchStr.empty() &&
              nameLower.find(searchStr) == std::string::npos &&
              fileLower.find(searchStr) == std::string::npos) {
            continue;
          }

          if (ImGui::TreeNode(name.c_str())) {
            // --- HEADER INFO ---
            std::string fileName = core::getFileName(image.filename);
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Source: %s",
                               fileName.c_str());
            app::tooltip(image.filename.c_str(), true, 0.5f);
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            // --- OUTER LAYOUT TABLE ---
            if (ImGui::BeginTable("##ImageAndMeta", 2,
                                  ImGuiTableFlags_SizingStretchProp)) {
              ImGui::TableNextRow();

              // --- LEFT COLUMN: IMAGE PREVIEW ---
              ImGui::TableNextColumn();
              ImTextureID gpuHandle =
                  m_renderer->deviceResources()->getTextureHandle(
                      image.textureId);

              if (gpuHandle) {
                float availWidth = ImGui::GetContentRegionAvail().x;
                float ratio = (image.width > 0)
                                  ? (float)image.height / (float)image.width
                                  : 1.0f;

                float displayWidth = std::min(availWidth * 0.70f, 200.0f);
                ImVec2 displaySize = ImVec2(displayWidth, displayWidth * ratio);

                ImGui::Image(gpuHandle, displaySize, ImVec2(0, 0), ImVec2(1, 1),
                             ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 0.3f));
              } else {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                   "Invalid GPU State");
              }

              // --- RIGHT COLUMN: METADATA ---
              ImGui::TableNextColumn();

              if (ImGui::BeginTable("##TexSpecs", 2,
                                    ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("Resolution:");
                ImGui::TableNextColumn();
                ImGui::Text("%u x %u", image.width, image.height);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("Format:");
                ImGui::TableNextColumn();
                ImGui::Text("%s", core::formatToString(image.format));

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("ID:");
                ImGui::TableNextColumn();
                ImGui::Text("%ld", image.textureId);

                ImGui::EndTable();
              }

              ImGui::EndTable(); // End Outer Table
            }

            ImGui::TreePop();
          }
        }
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }
  ImGui::End();
}

/**********************************************************/
void VulkanRendererElement::onUIMenu()
/**********************************************************/
{
  bool reload = false;
  if (ImGui::BeginMenu("Tools")) {
    reload |= ImGui::MenuItem("Reload Shaders", "F5");
    ImGui::EndMenu();
  }
  reload |= ImGui::IsKeyPressed(ImGuiKey_F5);

  if (reload)
    m_renderer->reload();
}

// ============================================================================
// 5. Accessors & Interaction
// ============================================================================

/**********************************************************/
const IRenderer *VulkanRendererElement::getRenderer() const
/**********************************************************/
{
  return m_renderer.get();
}

/**********************************************************/
const SceneManager &VulkanRendererElement::getSceneManager() const
/**********************************************************/
{
  return m_sceneManager;
}

/**********************************************************/
CameraPtr VulkanRendererElement::getCameraManipulator() const
/**********************************************************/
{
  return m_sceneManager.camera();
}

/**********************************************************/
void VulkanRendererElement::onGeometryPicked(InstanceID)
/**********************************************************/
{ /* Selection logic here */ }
