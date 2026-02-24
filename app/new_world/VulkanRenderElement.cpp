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
#include "app/widgets/texture_editor.hpp"
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
void VulkanRendererElement::onFileDrop(const std::filesystem::path &filename,
                                       glm::vec2 mousePos)
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
  } else if (ext == ".hdr") {
    LOGI("Env File dropped: %s\n", filename.c_str());
    m_envFileToLoad = filename;
  } else if (ext == ".png" || ext == ".jpg") {
    assert(m_geometryPicker);
    std::optional<InstanceID> id = m_geometryPicker->pickObject(mousePos);
    m_pendingTexture.emplace(
        PendingTexture{.filename = filename, .id = id.value_or(-1)});

  } else {
    LOGI("Error: Dropped file is not a recognised file ( %s )\n",
         filename.c_str());
  }
}

/**********************************************************/
void VulkanRendererElement::onFileSelected(
    const std::filesystem::path &filename)
/**********************************************************/
{
  onFileDrop(filename, {0, 0});
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
  try {
    loader.load(filepath, sceneData);
    LOGI("Successfully loaded: %s\n", filepath.c_str());
  } catch (const std::exception &e) {
    LOGE("Failed to load scene from %s: %s\n", filepath.c_str(), e.what());
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
  processPendingResources();

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
          if (auto mask = app::lightEditor(resourceManager,
                                           m_renderer->deviceResources())) {
            resourceManager.onLightChange(mask);
          }
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
        app::textureEditor(resourceManager, m_renderer->deviceResources());
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

/**********************************************************/
void VulkanRendererElement::processPendingResources()
/**********************************************************/
{
  auto &resourceMgr = m_sceneManager.sceneResourceManager();

  // 1. Scene Loading
  if (!m_sceneFile.empty()) {
    clear();
    loadScene(m_sceneFile);
    m_sceneFile.clear();
  }

  // 2. Model Loading
  if (!m_modelFileToLoad.empty()) {
    resourceMgr.loadModel(m_modelFileToLoad);
    resourceMgr.finalizeSceneResources();
    m_modelFileToLoad.clear();
  }

  // 3. Environment/Lighting Map
  if (!m_envFileToLoad.empty()) {
    resourceMgr.addEnvmap(m_envFileToLoad);
    resourceMgr.onLightChange(LightChangedBitMask::EnvmapChanged);
    m_envFileToLoad.clear();
  }

  // 4. Material Texture Assignment
  if (m_pendingTexture) {
    processPendingTexture(resourceMgr);
  }
}

/**********************************************************/
void VulkanRendererElement::processPendingTexture(
    SceneResourcesManager &resourceMgr)
/**********************************************************/
{
  const auto &filename = m_pendingTexture->filename;
  const auto instanceId = m_pendingTexture->id;

  // 1. Validation
  if (filename.empty()) {
    LOGW("Texture Load Warning: Empty filename provided (Instance ID: %d)\n",
         instanceId);
    m_pendingTexture.reset();
    return;
  }

  // 2. Resource Management
  std::string name = core::getLowercasedStem(filename);
  TextureID textureId = resourceMgr.addTexture(name, filename);

  if (textureId == -1) {
    LOGE("Texture Load Failure: Could not load '%s'\n", filename.c_str());
    m_pendingTexture.reset();
    return;
  }

  // Log successful addition
  LOGI("Texture Registered: [ID: %d] [Name: %s] [Path: %s]\n", textureId,
       name.c_str(), filename.c_str());

  resourceMgr.onTextureChange();

  // 3. Material Assignment (if an instance ID was provided)
  if (instanceId != -1) {
    auto &instances = resourceMgr.getInstances();

    if (instanceId < (int)instances.size()) {
      MaterialID materialId = instances[instanceId].materialIndex;
      auto &materials = resourceMgr.getMaterials();

      if (materialId < materials.size()) {
        materials[materialId].baseColorTextureIndex = textureId;
        resourceMgr.onMaterialChange();

        LOGI("Material Assignment: Instance %d (Material %d) updated with "
             "Texture %d\n",
             instanceId, materialId, textureId);
      } else {
        LOGW("Material Assignment Failure: Invalid Material ID %d for Instance "
             "%d\n",
             materialId, instanceId);
      }
    } else {
      LOGW("Material Assignment Failure: Invalid Instance ID %d\n", instanceId);
    }
  }

  m_pendingTexture.reset();
}
