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
#include <tinygltf/tiny_gltf.h>
#include <vulkan/vulkan.h>

#include <app/widgets/camera.hpp>
#include <app/widgets/property_editor.hpp>
#include <app/widgets/sky.hpp>
#include <app/widgets/tonemapper.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>

#include "app/App.hpp"
#include "backend/interfaces/IToneMapper.hpp"
#include "backend/vulkan/render/Renderer.hpp"
#include "core/Math.hpp"
#include "core/path_utils.hpp"
#include "core/timers.hpp"
#include "scene/SceneLoader.hpp"

/**********************************************************/
void VulkanRendererElement::setupScene(const std::filesystem::path &filename)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();
  SceneLoader loader;
  SceneData sceneData;
  auto filepath = nvutils::findFile(filename, common::getResourcesDirs());
  if (!loader.load(filepath, sceneData)) {
    return;
  }
  m_sceneManager.buildSceneFromData(sceneData, common::getResourcesDirs());

#define ADD_SPHERES
#ifdef ADD_SPHERES
  SceneResourcesManager &scene_resources =
      m_sceneManager.sceneResourceManager();
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> disColor(1e-5f, 1.0f);
  std::uniform_real_distribution<float> disPos(-20.0f, 20.0f);
  std::uniform_real_distribution<float> disScale(0.1f, 1.0f);

  const int numInstances = 400; // Increased count for better spiral effect
  const float goldenAngle = 2.39996323f; // radians (approx 137.5 degrees)
  const float spread = 0.8f;             // Controls spacing between spheres
  const float centerOpenRadius = 5.0f;

  for (int i = 0; i < numInstances; ++i) {
    // 1. Calculate Spiral Position (Phyllotaxis)
    float t = (float)i;
    float r = centerOpenRadius +
              (spread * std::sqrt(t)); // Radius grows with sqrt of index
    float theta = t * goldenAngle;

    float x = r * std::cos(theta);
    float z = r * std::sin(theta);

    // Cool Wave Effect: Bob the height (Y) based on distance from center
    float y = std::sin(r * 0.5f) * 2.0f + 2.0f;

    glm::vec3 pos = glm::vec3(x, y, z);

    // 2. Procedural Material Generation
    // Map color Hue to the angle (theta) for a rainbow spiral
    float hue = std::fmod(theta * 0.1f, 1.0f);
    glm::vec3 spiralColor =
        glm::vec3(0.5f + 0.5f * std::cos(6.28f * (hue + 0.0f)),  // R
                  0.5f + 0.5f * std::cos(6.28f * (hue + 0.33f)), // G
                  0.5f + 0.5f * std::cos(6.28f * (hue + 0.67f))  // B
        );

    // Make outer spheres denser/darker, inner spheres lighter
    float density = (r / 20.0f) * 10.0f;

    // Generate randomness for variation
    float randScale = disScale(gen);

    // Create Material
    std::string name = "Sphere" + std::to_string(i);
    MaterialID randomMatId = scene_resources.addMaterial(
        {.baseColorFactor = glm::vec4(spiralColor, 1.0f),
         .metallicFactor =
             (i % 2 == 0) ? 1.0f : 0.0f,        // Alternate metal/dielectric
         .roughnessFactor = 0.1f + (r / 30.0f), // Smoother in center
         .sigma_t = density},
        name);

    // 3. Add the Instance
    scene_resources.addInstance(
        {.translation = pos,
         .scale = glm::vec3(randScale * 0.8f), // Slightly uniform scale
         .materialIndex = randomMatId,
         .meshIndex = scene_resources.getMeshIDFromName("sphere"),
         .hit_group = MaterialType::eVolumetric},
        name);
  }

#endif

  // build scene
  m_sceneManager.sceneResourceManager().finalizeSceneResources();
  m_renderer->init(m_sceneManager.sceneResourceManager());
}

/**********************************************************/
void VulkanRendererElement::onAttach(core::Application *app)
/**********************************************************/
{
  m_app = app;
  auto *backend = dynamic_cast<VulkanBackend *>(app->getBackend());
  assert(backend && "Backend is not VulkanBackend");
  m_renderer = std::make_shared<VulkanRenderer>(backend);
  m_sceneManager = SceneManager(m_renderer);
  setupScene();
}

/**********************************************************/
void VulkanRendererElement::onDetach()
/**********************************************************/
{
  m_renderer->deinit();
  m_sceneManager.clear();
}

/**********************************************************/
void VulkanRendererElement::onResize(WindowSize size)
/**********************************************************/
{
  m_renderer->onResize(size);
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

  if (reload) {
    m_renderer->reload();
  }
}

void VulkanRendererElement::onUIRender() {
  namespace PE = core::PropertyEditor;
  m_hasChanged = false;

  // --- Viewport Window ---
  if (ImGui::Begin("Viewport")) {
    ImGui::Image(
        ImTextureID(m_renderer->getImageDescriptor(RenderOutput::ToneMapped)),
        ImGui::GetContentRegionAvail());
  }
  ImGui::End();

  // --- Main Control Window ---
  if (ImGui::Begin("Settings")) {
    // 1. Setup the Tab Bar
    if (ImGui::BeginTabBar("MainTabs")) {
      // --- TAB 1: SCENE & RENDERING ---
      if (ImGui::BeginTabItem("Render")) {
        // Render Mode Selection
        const char *preview = renderModeToString(m_renderMode);
        if (ImGui::BeginCombo("Render Mode", preview)) {
          for (int n = 0; n < static_cast<int>(RenderMode::COUNT); n++) {
            auto mode = static_cast<RenderMode>(n);
            if (ImGui::Selectable(renderModeToString(mode),
                                  m_renderMode == mode)) {
              m_renderMode = mode;
              m_renderer->setRenderMode(m_renderMode);
              m_sceneManager.sceneResourceManager().setDirty(true);
            }
          }
          ImGui::EndCombo();
        }

        if (ImGui::CollapsingHeader("Tonemapper")) {
          core::tonemapperWidget(m_renderer->postProcessor().data());
        }

        if (m_renderMode == RenderMode::RAYTRACE) {
          if (ImGui::CollapsingHeader("Integrator Params")) {
            PE::begin();
            shaderio::RenderParams &params = m_renderer->renderParams();
            m_hasChanged |=
                PE::DragInt("Samples", &params.nSamples, 1.0F, 0, 1024);
            m_hasChanged |=
                PE::DragInt("Max Bounces", &params.maxBounces, 1.0F, 0, 1024);
            if (PE::Button("Reset Accumulation", ImVec2(-1.0f, 0.0f))) {
              m_hasChanged = true;
            }
            PE::end();
          }
        } else {
          if (ImGui::CollapsingHeader("Rasterizer Params")) {
            PE::begin();
            shaderio::RasterParams &params = m_renderer->rasterParams();
            if (PE::Checkbox("Wireframe Mode", (bool *)&params.wireframe)) {
              m_hasChanged = true;
            }
            if (params.wireframe) {
              if (PE::SliderFloat("Line Width", &params.wireframeLineWidth,
                                  0.1f, 10.0f)) {
                m_hasChanged = true;
              }
              ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                 "Note: Wide lines require hardware support.");
            }

            PE::end();
          }
        }

        ImGui::EndTabItem();
      }

      // --- TAB 2: ENVIRONMENT & LIGHTING ---
      if (ImGui::BeginTabItem("SceneInfo")) {
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
          m_hasChanged |= core::CameraWidget(m_sceneManager.camera());
        }

        auto &sceneInfo = m_sceneManager.sceneInfo();
        if (ImGui::CollapsingHeader("Environment")) {
          auto &sceneInfo = m_sceneManager.sceneInfo();
          m_hasChanged |= ImGui::Checkbox("Use Sky", (bool *)&sceneInfo.useSky);
          if (sceneInfo.useSky) {
            m_hasChanged |=
                core::skySimpleParametersUI(sceneInfo.skySimpleParam);
          } else {

            PE::begin();
            m_hasChanged |= PE::ColorEdit3("Background",
                                           (float *)&sceneInfo.backgroundColor);
            PE::end();
            auto &light = sceneInfo.punctualLights[0];
            PE::begin();
            if (light.type == shaderio::LightType::ePoint ||
                light.type == shaderio::LightType::eSpot) {
              m_hasChanged |= PE::DragFloat3("Light Position",
                                             glm::value_ptr(light.position),

                                             0.1f, -20.0f, 20.0f);
            }
            if (light.type == shaderio::LightType::eDirectional ||
                light.type == shaderio::LightType::eSpot) {
              m_hasChanged |= PE::SliderFloat3("Light Direction",
                                               glm::value_ptr(light.direction),
                                               -1.0f, 1.0f);
            }

            m_hasChanged |=
                PE::SliderFloat("Light Intensity", &light.intensity, 0.0f,
                                1000.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            m_hasChanged |=
                PE::ColorEdit3("Light Color", glm::value_ptr(light.color),
                               ImGuiColorEditFlags_NoInputs);

            // Using a temp int for the combo to capture changes properly
            int typeInt = static_cast<int>(light.type);
            if (PE::Combo("Light Type", &typeInt, "Point\0Spot\0Directional\0",
                          3)) {
              light.type = static_cast<shaderio::LightType>(typeInt);
              m_hasChanged = true;
            }

            if (light.type == shaderio::LightType::eSpot) {
              m_hasChanged |=
                  PE::SliderAngle("Cone Angle", &light.coneAngle, 0.f, 90.f);
            }
            PE::end();
          }
        }
        ImGui::EndTabItem();
      }

      // --- TAB 3: ASSETS
      if (ImGui::BeginTabItem("Materials")) {
        renderMaterialsUI();
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Instances")) {
        renderInstancesUI();
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar(); // Close the Tab Bar
    }
  }
  ImGui::End();
}

/**********************************************************/
void VulkanRendererElement::onPreRender()
/**********************************************************/
{
  m_hasChanged |= m_sceneManager.camera()->isDirty();
  if (m_hasChanged) {
    m_renderer->reset();
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
  shaderio::PushConstant pushValues{};
  IRenderContext &ctx_ref = const_cast<IRenderContext &>(ctx);
  ctx_ref.pushValues = pushValues;
  ctx_ref.sceneResources = m_sceneManager.getScenePtr();
  m_renderer->render(ctx_ref);
}

/**********************************************************/
void VulkanRendererElement::onEndFrame(const IRenderContext &ctx)
/**********************************************************/
{}

/**********************************************************/
void VulkanRendererElement::onLastHeadlessFrame()
/**********************************************************/
{
  m_renderer->saveImage(
      nvutils::getExecutablePath().replace_extension(".jpg").string());
}

/**********************************************************/
CameraPtr VulkanRendererElement::getCameraManipulator() const
/**********************************************************/
{
  return m_sceneManager.camera();
}

/**********************************************************/
void VulkanRendererElement::onFileDrop(const std::filesystem::path &filename)
/**********************************************************/
{
  std::cout << "File dropped: " << filename << std::endl;
}

/**********************************************************/
void VulkanRendererElement::renderMaterialsUI()
/**********************************************************/
{
  namespace PE = core::PropertyEditor;
  auto &resources = m_sceneManager.sceneResourceManager();
  auto &materials = resources.getMaterials();
  const auto &materialMap = resources.materialMap();
  bool changed = false;

  // Static buffer to persist search text between frames
  static char materialSearch[128] = "";

  if (ImGui::CollapsingHeader("Materials")) {
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
        changed |= PE::ColorEdit3("Emission", glm::value_ptr(mat.emission));

        // Show IOR - typically we only edit x for simple dielectrics
        changed |= PE::SliderFloat3("IOR (Spectral)", glm::value_ptr(mat.ior),
                                    1.0f, 2.5f);

        // Read-only info
        PE::Text("Texture Index",
                 fmt::format("{}", mat.baseColorTextureIndex).c_str());

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
  namespace PE = core::PropertyEditor;
  auto &resources = m_sceneManager.sceneResourceManager();
  auto &instances = resources.getInstances();
  const auto &materials = resources.getMaterials();
  const auto &materialMap = resources.materialMap();
  const auto &instanceMap = resources.instanceMap();
  const auto &shaderRegistry = m_renderer->getShaderManager().getRegistry();

  updateMaterialList();

  bool changed = false;
  static char instanceSearch[128] = "";

  if (ImGui::CollapsingHeader("Instances")) {
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
void VulkanRendererElement::onGeometryPicked(InstanceID id)
/**********************************************************/
{
  // m_instanceSelected = id;
}

/**********************************************************/
void VulkanRendererElement::updateMaterialList()
/**********************************************************/
{
  auto &resources = m_sceneManager.sceneResourceManager();
  const auto &materialMap = resources.materialMap();

  if (materialMap.size() == m_matIDs.size()) {
    return;
  }

  m_matIDs.clear();
  m_matNamesList.clear();
  m_matIDToIndex.clear();

  int counter = 0;
  for (auto const &[matName, mId] : materialMap) {
    m_matNamesList += matName + '\0'; // Add to ImGui buffer
    m_matIDs.push_back(mId);

    m_matIDToIndex[mId] = counter;
    counter++;
  }
  m_matNamesList += '\0'; // Final null terminator for ImGui
}
