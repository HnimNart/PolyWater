#include "VulkanRenderElement.hpp"

// Standard Libs
#include <cstdio>
#include <fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <random>

// Third Party
#include <imgui/imgui.h>
#include <shaders/shared/structs.h>
#include <vulkan/vulkan.h>

#include <app/widgets/camera.hpp>
#include <app/widgets/property_editor.hpp>
#include <app/widgets/sky.hpp>
#include <app/widgets/tonemapper.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>

#include "app/Application.hpp"
#include "app/widgets/axis.hpp"
#include "backend/vulkan/core/Backend.hpp"
#include "core/Math.hpp"
#include "core/path_utils.hpp"
#include "core/string_utils.h"
#include "core/timers.hpp"
#include "renderer/interfaces/IToneMapper.hpp"
#include "renderer/vulkan/Renderer.hpp"
#include "scene/SceneLoader.hpp"

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
void VulkanRendererElement::reset()
/**********************************************************/
{
  m_renderer->deinit();
  auto *backend = dynamic_cast<VulkanBackend *>(m_app->getBackend());
  m_renderer =
      std::make_unique<VulkanRenderer>(backend, common::getShaderDirs());

  m_sceneManager.clear();
  m_sceneManager.sceneResourceManager().init(m_renderer->deviceResources());
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
    std::cout << "Scene File dropped: " << filename << std::endl;
    if (m_sceneFile == filename) {
      return;
    }
    m_sceneFile = filename;
    reset();
    loadScene(m_sceneFile);
    onResize(m_app->getViewportSize());
  } else {
    std::cerr << "Error: Dropped file is not a JSON file (" << ext << ")"
              << std::endl;
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
           .metallicFactor = (i % 2 == 0) ? 1.0f : 0.0f,
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
}

/**********************************************************/
void VulkanRendererElement::updateMaterialList()
/**********************************************************/
{
  auto &resources = m_sceneManager.sceneResourceManager();
  const auto &materialMap = resources.materialMap();

  if (materialMap.size() == m_matIDs.size())
    return;

  m_matIDs.clear();
  m_matNamesList.clear();
  m_matIDToIndex.clear();

  int counter = 0;
  for (auto const &[matName, mId] : materialMap) {
    m_matNamesList += matName + '\0';
    m_matIDs.push_back(mId);
    m_matIDToIndex[mId] = counter++;
  }
  m_matNamesList += '\0';
}

// ============================================================================
// 3. Frame Loop Callbacks
// ============================================================================

/**********************************************************/
void VulkanRendererElement::onPreRender()
/**********************************************************/
{
  m_hasChanged |= m_sceneManager.camera()->isDirty();

  if (m_hasChanged) {
    m_renderer->reset(); // Resets accumulation
    m_sceneManager.camera()->setClean();
  }

  m_sceneManager.update();

  if (m_sceneManager.sceneResourceManager().dirty()) {
    m_renderer->update(m_sceneManager.sceneResourceManager());
    m_sceneManager.sceneResourceManager().setDirty(false);
  }
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

  // --- VIEWPORT WINDOW ---
  if (ImGui::Begin("Viewport") && !m_app->isPaused()) {
    ImTextureID toneMappedId =
        ImTextureID(m_renderer->getImageDescriptor(RenderOutput::ToneMapped));
    ImGui::Image(toneMappedId, ImGui::GetContentRegionAvail());
    app::drawAxis(m_sceneManager.camera()->getViewProjection());
  }
  ImGui::End();

  // --- SETTINGS WINDOW ---
  if (ImGui::Begin("Settings")) {
    if (ImGui::BeginTabBar("MainTabs")) {

      // --- TAB 1: RENDERING ---
      if (ImGui::BeginTabItem("Render")) {
        if (PE::begin("RenderModeTable")) {
          const char *preview = renderModeToString(m_renderMode);
          if (PE::entry("Render Mode", [&]() {
                bool changed = false;
                if (ImGui::BeginCombo("##mode", preview)) {
                  for (int n = 0; n < static_cast<int>(RenderMode::COUNT);
                       n++) {
                    auto mode = static_cast<RenderMode>(n);
                    if (ImGui::Selectable(renderModeToString(mode),
                                          m_renderMode == mode)) {
                      m_renderMode = mode;
                      m_renderer->setRenderMode(m_renderMode);
                      m_sceneManager.sceneResourceManager().setDirty(true);
                      changed = true;
                    }
                  }
                  ImGui::EndCombo();
                }
                return changed;
              })) {
            m_hasChanged = true;
          }
          PE::end();
        }

        if (ImGui::CollapsingHeader("Tonemapper",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
          core::tonemapperWidget(m_renderer->postProcessor().data());
        }

        if (m_renderMode == RenderMode::RAYTRACE) {
          if (ImGui::CollapsingHeader("Integrator Params",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            if (PE::begin("IntegratorTable")) {
              auto &params = m_renderer->renderParams();
              m_hasChanged |=
                  PE::DragInt("Samples", &params.nSamples, 1.0f, 1, 1024);
              m_hasChanged |=
                  PE::DragInt("Max Bounces", &params.maxBounces, 1.0f, 0, 32);
              if (PE::Button("Reset Accumulation", ImVec2(-1.0f, 0.0f)))
                m_hasChanged = true;
              PE::end();
            }
          }
        }
        ImGui::EndTabItem();
      }

      // --- TAB 2: ENVIRONMENT & LIGHTING ---
      if (ImGui::BeginTabItem("SceneInfo")) {
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
          m_hasChanged |= app::cameraWidget(m_sceneManager.camera());
        }

        auto &sceneInfo = m_sceneManager.sceneInfo();
        if (ImGui::CollapsingHeader("Environment",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {

          // Environment Type Selection (Radio Buttons)
          if (PE::begin("EnvTypeTable")) {
            if (PE::RadioButton("Physical Sky", sceneInfo.useSky)) {
              sceneInfo.useSky = true;
              sceneInfo.useEnv = false;
              m_hasChanged = true;
            }
            if (sceneInfo.envmapLight.totalSum > 0 &&
                PE::RadioButton("HDR Environment", sceneInfo.useEnv)) {
              sceneInfo.useEnv = true;
              sceneInfo.useSky = false;
              m_hasChanged = true;
            }
            bool useSolid = !sceneInfo.useSky && !sceneInfo.useEnv;
            if (PE::RadioButton("Solid Color", useSolid)) {
              sceneInfo.useSky = false;
              sceneInfo.useEnv = false;
              m_hasChanged = true;
            }
            PE::end();
          }

          ImGui::Separator();

          // Specific Settings based on selection
          if (sceneInfo.useSky) {
            m_hasChanged |=
                app::skySimpleParametersUI(sceneInfo.skySimpleParam);
          } else if (sceneInfo.useEnv) {
            auto &env = sceneInfo.envmapLight;
            if (PE::begin("EnvParamsTable")) {
              m_hasChanged |=
                  PE::DragFloat("Intensity", &env.scale, 0.1f, 0.0f, 10.0f);
              if ((m_hasChanged |= PE::DragFloat("Rotation (Azimuth)",
                                                 &env.rotationAzimuthDegree,
                                                 1.0f, 0.0f, 360.0f))) {
                env.rotation = glm::rotate(
                    glm::mat4(1.0f), glm::radians(env.rotationAzimuthDegree),
                    glm::vec3(0, 1, 0));
              }

              if (PE::treeNode("Technical Info")) {
                PE::Text("Resolution", "%u x %u", env.dims.x, env.dims.y);
                PE::Text("Tex Index", "%d", env.envTextureIdx);
                PE::Text("Integral", "%.4f", env.totalSum);
                PE::treePop();
              }

              if (PE::Button(
                      "Reload Map",
                      ImVec2(-1, 0))) { /* m_sceneManager.reloadEnvmap(); */
              }
              PE::end();
            }
          } else {
            if (PE::begin("SolidColorTable")) {
              m_hasChanged |= PE::ColorEdit3(
                  "Background Color", (float *)&sceneInfo.backgroundColor);
              PE::end();
            }
          }

          // Punctual Light Section
          if (ImGui::TreeNodeEx("Punctual Light",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
            auto &light = sceneInfo.punctualLights[0];
            if (PE::begin("LightTable")) {
              int typeInt = static_cast<int>(light.type);
              if (PE::Combo("Type", &typeInt, "Point\0Spot\0Directional\0")) {
                light.type = (shaderio::LightType)typeInt;
                m_hasChanged = true;
              }

              if (light.type != shaderio::LightType::eDirectional)
                m_hasChanged |= PE::DragFloat3(
                    "Position", glm::value_ptr(light.position), 0.1f);
              if (light.type != shaderio::LightType::ePoint)
                m_hasChanged |= PE::SliderFloat3(
                    "Direction", glm::value_ptr(light.direction), -1.0f, 1.0f);

              m_hasChanged |=
                  PE::DragFloat("Intensity", &light.intensity, 1.0f, 0.0f,
                                10000.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
              m_hasChanged |=
                  PE::ColorEdit3("Color", glm::value_ptr(light.color));

              if (light.type == shaderio::LightType::eSpot)
                m_hasChanged |= PE::SliderAngle("Cone Angle", &light.coneAngle,
                                                0.0f, 90.0f);

              PE::end();
            }
            ImGui::TreePop();
          }
        }
        ImGui::EndTabItem();
      }

      // --- OTHER TABS ---
      if (ImGui::BeginTabItem("Materials")) {
        renderMaterialsUI();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Instances")) {
        renderInstancesUI();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Meshes")) {
        renderMeshesUI();
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }
    ImGui::End();
  }
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

/**********************************************************/
void VulkanRendererElement::renderMaterialsUI()
/**********************************************************/
{
  namespace PE = app::PropertyEditor;
  auto &resources = m_sceneManager.sceneResourceManager();
  auto &materials = resources.getMaterials();
  const auto &materialMap = resources.materialMap();
  bool changed = false;

  // Static buffer to persist search text between frames
  static char materialSearch[128] = "";

  if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
    // 1. Search Bar
    ImGui::InputTextWithHint("##MatSearch", "Filter by name...", materialSearch,
                             IM_ARRAYSIZE(materialSearch));
    ImGui::Separator();

    // Prepare search string for case-insensitive comparison
    std::string searchStr = materialSearch;
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(),
                   ::tolower);

    // 2. Iterate through the map (already alphabetical)
    for (const auto &[name, id] : materialMap) {
      if (id >= materials.size())
        continue;

      // Apply Search Filter
      std::string nameLower = name;
      std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                     ::tolower);
      if (!searchStr.empty() &&
          nameLower.find(searchStr) == std::string::npos) {
        continue;
      }

      // Use name and ID for a unique ImGui ID
      std::string label = fmt::format("{} (ID: {})", name, id);

      if (ImGui::TreeNode(label.c_str())) {
        auto &mat = materials[id];
        PE::begin();

        changed |=
            PE::ColorEdit4("Base Color", glm::value_ptr(mat.baseColorFactor));
        changed |= PE::SliderFloat("Metallic", &mat.metallicFactor, 0.0f, 1.0f);
        changed |=
            PE::SliderFloat("Roughness", &mat.roughnessFactor, 0.0f, 1.0f);
        changed |= PE::SliderFloat3("Emission", glm::value_ptr(mat.emission),
                                    0.0F, 100.F);
        changed |= PE::SliderFloat3("IOR (Spectral)", glm::value_ptr(mat.ior),
                                    1.0f, 2.5f);
        changed |= PE::SliderFloat3("Extinction", glm::value_ptr(mat.sigma_t),
                                    0.0f, 100.0f);
        changed |= PE::SliderFloat3("Asymmetry", glm::value_ptr(mat.asymmetry),
                                    0.0f, 1.0f);

        const auto &textureMap = resources.textureMap();
        std::string currentName = "None";
        for (const auto &[name, id] : textureMap) {
          if (id == mat.baseColorTextureIndex) {
            currentName = name;
            break;
          }
        }

        if (ImGui::BeginCombo("Base Color Texture", currentName.c_str())) {
          for (const auto &[name, id] : textureMap) {
            const bool isSelected = (currentName == name);
            if (ImGui::Selectable(name.c_str(), isSelected)) {
              mat.baseColorTextureIndex = id;
              m_hasChanged = true;
              resources.onMaterialChange();
            }

            if (isSelected) {
              ImGui::SetItemDefaultFocus();
            }
          }
          ImGui::EndCombo();
        }

        PE::end();
        ImGui::TreePop();
      }
    }
  }

  if (changed) {
    resources.onMaterialChange();
  }
}

/**********************************************************/
void VulkanRendererElement::renderInstancesUI()
/**********************************************************/
{
  namespace PE = app::PropertyEditor;
  auto &resources = m_sceneManager.sceneResourceManager();
  auto &instances = resources.getInstances();
  const auto &materials = resources.getMaterials();
  const auto &instanceMap = resources.instanceMap();
  const auto &shaderRegistry = m_renderer->getShaderManager().getRegistry();

  updateMaterialList();

  bool changed = false;
  static char instanceSearch[128] = "";

  if (ImGui::CollapsingHeader("Instances", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::InputTextWithHint("##InstSearch", "Search instances...",
                             instanceSearch, IM_ARRAYSIZE(instanceSearch));
    ImGui::Separator();

    std::string searchStr = instanceSearch;
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(),
                   ::tolower);

    // Iterate through the map
    for (const auto &[name, id] : instanceMap) {
      if (id >= instances.size())
        continue;

      // Filter
      std::string nameLower = name;
      std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                     ::tolower);
      if (!searchStr.empty() && nameLower.find(searchStr) == std::string::npos)
        continue;

      std::string label = fmt::format("{}[{}]##{}", name, id, id);
      if (ImGui::TreeNode(label.c_str())) {
        auto &inst = instances[id];
        int matIdx = static_cast<int>(inst.materialIndex);
        PE::begin();

        // Find current id of the selected material
        int currentComboItem = -1;
        if (auto it = m_matIDToIndex.find(inst.materialIndex);
            it != m_matIDToIndex.end()) {
          currentComboItem = it->second;
        }

        // Draw the Dropdown
        if (PE::Combo("Material Select", &currentComboItem,
                      m_matNamesList.c_str(), (int)m_matIDs.size())) {
          inst.materialIndex = m_matIDs[currentComboItem];
          matIdx = (int)inst.materialIndex; // Sync slider
          changed = true;
        }

        // Draw the Slider
        if (PE::SliderInt("Material ID", &matIdx, 0,
                          (int)materials.size() - 1)) {
          inst.materialIndex = (uint32_t)matIdx;
          changed = true;
        }

        // 2. Hit Group (Shader) Assignment
        std::vector<MaterialType> types;
        std::string shaderNames;
        int currentTypeIdx = -1;
        int count = 0;
        for (auto const &[type, entry] : shaderRegistry) {
          if (type == inst.hit_group)
            currentTypeIdx = count;
          shaderNames += entry.prettyName + '\0';
          types.push_back(type);
          count++;
        }
        shaderNames += '\0';

        if (PE::Combo("Shader Type", &currentTypeIdx, shaderNames.c_str(),
                      (int)types.size())) {
          inst.hit_group = types[currentTypeIdx];
          changed = true;
        }

        // 3. Transformation
        PE::Text("Mesh ID", fmt::format("{}", inst.meshIndex).c_str());

        glm::quat rotation = math::toQuat(glm::vec4(inst.rotation));
        glm::vec3 rotationEuler = glm::degrees(glm::eulerAngles(rotation));

        bool tChanged =
            PE::DragFloat3("Position", glm::value_ptr(inst.translation), 0.1f);
        bool rChanged =
            PE::DragFloat3("Rotation", glm::value_ptr(rotationEuler), 0.5f);
        bool sChanged =
            PE::DragFloat3("Scale", glm::value_ptr(inst.scale), 0.05f);

        if (tChanged || rChanged || sChanged) {
          glm::quat quat = glm::quat(glm::radians(rotationEuler));
          inst.rotation = math::fromQuat(quat);
          inst.transform = math::composeTransform(inst.translation,
                                                  inst.rotation, inst.scale);
          changed = true;
        }

        PE::end();
        ImGui::TreePop();
      }
    }
  }

  if (changed) {
    resources.onInstanceChange();
  }
}

/**********************************************************/
void VulkanRendererElement::renderMeshesUI()
/**********************************************************/
{
  namespace PE = app::PropertyEditor;
  auto &resources = m_sceneManager.sceneResourceManager();
  auto &meshes = resources.getMeshes();
  const auto &meshMap = resources.meshMap();

  static char meshSearch[128] = "";

  if (ImGui::CollapsingHeader("Meshes", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::InputTextWithHint("##MeshSearch", "Search meshes...", meshSearch,
                             IM_ARRAYSIZE(meshSearch));
    ImGui::Separator();

    std::string searchStr = meshSearch;
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(),
                   ::tolower);

    for (const auto &[name, id] : meshMap) {
      if (id >= meshes.size())
        continue;

      // Filter logic
      std::string nameLower = name;
      std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                     ::tolower);
      if (!searchStr.empty() && nameLower.find(searchStr) == std::string::npos)
        continue;

      std::string label = fmt::format("{}##mesh_{}", name, id);

      // 1. Root Mesh Node
      if (ImGui::TreeNode(label.c_str())) {
        const auto &mesh = meshes[id];
        PE::begin();
        PE::Text("Mesh ID", fmt::format("{}", id).c_str());
        PE::Text("Vertices",
                 fmt::format("{}", mesh.triMesh.positions.count).c_str());
        PE::Text("Indices",
                 fmt::format("{}", mesh.triMesh.indices.count).c_str());
        const shaderio::BoundingBox &bbox = mesh.bbox;
        PE::Text("BBox Min", fmt::format("{:.1f}, {:.1f}, {:.1f}", bbox.min.x,
                                         bbox.min.y, bbox.min.z)
                                 .c_str());
        PE::Text("BBox Max", fmt::format("{:.1f}, {:.1f}, {:.1f}", bbox.max.x,
                                         bbox.max.y, bbox.max.z)
                                 .c_str());
        PE::end();

        ImGui::TreePop();
      }
    }
  }
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
