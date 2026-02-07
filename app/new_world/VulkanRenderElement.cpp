#include "VulkanRenderElement.hpp"

// Standard Libs
#include <cstdio>
#include <fmt/format.h>
#include <iostream>

// Third Party
#include <imgui/imgui.h>
#include <shaders/shaderio.h>
#include <tinygltf/tiny_gltf.h>
#include <vulkan/vulkan.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <nvgui/camera.hpp>
#include <nvgui/property_editor.hpp>
#include <nvgui/sky.hpp>
#include <nvgui/tonemapper.hpp>

#include "backend/interfaces/IToneMapper.hpp"
#include "backend/vulkan/render/Renderer.hpp"
#include "common/path_utils.hpp"
#include "common/timers.hpp"
#include "core/Math.hpp"
#include "core/application/App.hpp"

/**********************************************************/
void VulkanRendererElement::setupScene()
/**********************************************************/
{
  SCOPED_TIMER_FUNC();
  SceneResourcesManager &scene_resources =
      m_scene_manager.sceneResourceManager();

  // Load the GLTF resources
  tinygltf::Model teapotModel = scene_resources.loadGltf(
      nvutils::findFile("teapot.gltf", common::getResourcesDirs()));

  tinygltf::Model planeModel = scene_resources.loadGltf(
      nvutils::findFile("plane.gltf", common::getResourcesDirs()));

  // Textures
  IDeviceAssets::TextureID texture_id = scene_resources.loadTexture(
      nvutils::findFile("tiled_floor.png", common::getResourcesDirs()));

  // Teapot material
  SceneResourcesManager::MaterialID teapot_id = scene_resources.addMaterial(
      {.baseColorFactor = glm::vec4(0.8f, 1.0f, 0.6f, 1.0f),
       .metallicFactor = 0.5f,
       .roughnessFactor = 0.5f});
  // Plane material with texture
  SceneResourcesManager::MaterialID plane_id = scene_resources.addMaterial(
      {.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
       .metallicFactor = 0.1f,
       .roughnessFactor = 0.8f,
       .baseColorTextureIndex = static_cast<int>(texture_id)});

  // Teapot
  scene_resources.addInstance({.position = glm::vec3(0, 0, 0),
                               .scale = glm::vec3(0.5f),
                               .materialIndex = teapot_id,
                               .meshIndex = 0,
                               .hit_group = MaterialType::eGltfPbr});
  // Plane
  scene_resources.addInstance({.position = glm::vec3(0.0f, -0.9f, 0.0f),
                               .scale = glm::vec3(2.0f),
                               .materialIndex = plane_id,
                               .meshIndex = 1,
                               .hit_group = MaterialType::eGltfPbr});

  // Scene information
  shaderio::SceneInfo &sceneInfo = m_scene_manager.sceneInfo();
  sceneInfo.useSky = false;                          // Use light
  sceneInfo.backgroundColor = {0.85f, 0.85f, 0.85f}; // The background color
  sceneInfo.numLights = 1;
  sceneInfo.punctualLights[0].color = glm::vec3(1.0f, 1.0f, 1.0f);
  sceneInfo.punctualLights[0].intensity = 4.0f;
  sceneInfo.punctualLights[0].position =
      glm::vec3(1.0f, 1.0f, 1.0f); // Position of the light
  sceneInfo.punctualLights[0].direction =
      glm::vec3(1.0f, 1.0f, 1.0f); // Direction to the light
  sceneInfo.punctualLights[0].type = shaderio::GltfLightType::ePoint;
  sceneInfo.punctualLights[0].coneAngle =
      0.9f; // Cone angle for spot lights (0 for point and
            // directional lights)

  scene_resources.finalizeSceneResources();

  // Default camera
  m_scene_manager.camera()->setClipPlanes({0.01F, 100.0F});
  m_scene_manager.camera()->setLookat({0.0F, 0.5F, 5.0}, {0.F, 0.F, 0.F},
                                      {0.0F, 1.0F, 0.0F});
  m_scene_manager.camera()->setClean();

  // build scene
  m_renderer->init(m_scene_manager.sceneResourceManager());
}

/**********************************************************/
void VulkanRendererElement::onAttach(core::Application *app)
/**********************************************************/
{
  m_app = app;
  auto *backend = dynamic_cast<VulkanBackend *>(app->getBackend());
  assert(backend && "Backend is not VulkanBackend");
  m_renderer = std::make_shared<VulkanRenderer>(backend);
  m_scene_manager = SceneManager(m_renderer);
  setupScene();
}

/**********************************************************/
void VulkanRendererElement::onDetach()
/**********************************************************/
{
  m_renderer->deinit();
  m_scene_manager.clear();
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
    m_renderer->reload(m_scene_manager.sceneResourceManager());
  }
}

/**********************************************************/
void VulkanRendererElement::onUIRender()
/**********************************************************/
{
  namespace PE = nvgui::PropertyEditor;
  m_hasChanged = false;
  if (ImGui::Begin("Viewport")) {
    ImGui::Image(
        ImTextureID(m_renderer->getImageDescriptor(RenderOutput::ToneMapped)),
        ImGui::GetContentRegionAvail());
  }
  ImGui::End();

  if (ImGui::Begin("Settings")) {
    const char *preview = renderModeToString(m_renderMode);

    if (ImGui::BeginCombo("Render Mode", preview)) {
      for (int n = 0; n < static_cast<int>(RenderMode::COUNT); n++) {
        auto mode = static_cast<RenderMode>(n);
        bool isSelected = (m_renderMode == mode);
        if (ImGui::Selectable(renderModeToString(mode), isSelected)) {
          m_renderMode = mode;
          m_renderer->setRenderMode(m_renderMode,
                                    m_scene_manager.sceneResourceManager());
        }
        if (isSelected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    if (ImGui::CollapsingHeader("Camera")) {
      m_hasChanged |= nvgui::CameraWidget(m_scene_manager.camera());
    }

    if (ImGui::CollapsingHeader("Environment")) {
      auto &sceneInfo = m_scene_manager.sceneInfo();
      m_hasChanged |= ImGui::Checkbox("Use Sky", (bool *)&sceneInfo.useSky);

      if (sceneInfo.useSky) {
        m_hasChanged |= nvgui::skySimpleParametersUI(sceneInfo.skySimpleParam);
      } else {
        PE::begin();
        m_hasChanged |=
            PE::ColorEdit3("Background", (float *)&sceneInfo.backgroundColor);
        PE::end();

        auto &light = sceneInfo.punctualLights[0];
        PE::begin();
        if (light.type == shaderio::GltfLightType::ePoint ||
            light.type == shaderio::GltfLightType::eSpot) {
          m_hasChanged |=
              PE::DragFloat3("Light Position", glm::value_ptr(light.position),
                             0.1f, -20.0f, 20.0f);
        }
        if (light.type == shaderio::GltfLightType::eDirectional ||
            light.type == shaderio::GltfLightType::eSpot) {
          m_hasChanged |= PE::SliderFloat3(
              "Light Direction", glm::value_ptr(light.direction), -1.0f, 1.0f);
        }

        m_hasChanged |=
            PE::SliderFloat("Light Intensity", &light.intensity, 0.0f, 1000.0f,
                            "%.2f", ImGuiSliderFlags_Logarithmic);
        m_hasChanged |=
            PE::ColorEdit3("Light Color", glm::value_ptr(light.color),
                           ImGuiColorEditFlags_NoInputs);

        // Using a temp int for the combo to capture changes properly
        int typeInt = static_cast<int>(light.type);
        if (PE::Combo("Light Type", &typeInt, "Point\0Spot\0Directional\0",
                      3)) {
          light.type = static_cast<shaderio::GltfLightType>(typeInt);
          m_hasChanged = true;
        }

        if (light.type == shaderio::GltfLightType::eSpot) {
          m_hasChanged |=
              PE::SliderAngle("Cone Angle", &light.coneAngle, 0.f, 90.f);
        }
        PE::end();
      }
    }

    if (ImGui::CollapsingHeader("Tonemapper")) {
      nvgui::tonemapperWidget(m_renderer->postProcessor().data());
    }

    if (ImGui::CollapsingHeader("Render")) {
      PE::begin();
      shaderio::RenderParams &params = m_renderer->renderParams();
      m_hasChanged |=
          PE::DragInt("Number of samples", &params.nSamples, 1.0F, 0, 1024);
      m_hasChanged |=
          PE::DragInt("Max Bounces", &params.maxBounces, 1.0F, 0, 1024);
      m_hasChanged |=
          PE::DragInt("RR threshold", &params.nBouncesRR, 1.0F, 0, 1024);

      // Manual reset button
      if (PE::Button("Reset Accumulation", ImVec2(-1, 0),
                     "Clear the accumulation buffer and start over")) {
        m_hasChanged = true;
      }
      PE::end();
    }

    renderMaterials();
    renderInstances();
  }
  ImGui::End();
}

/**********************************************************/
void VulkanRendererElement::onPreRender()
/**********************************************************/
{
  m_hasChanged |= m_scene_manager.camera()->isDirty();
  if (m_hasChanged) {
    m_renderer->reset();
    m_scene_manager.camera()->setClean();
  }
  m_scene_manager.update();
  if (m_scene_manager.sceneResourceManager().dirty()) {
    m_renderer->update(m_scene_manager.sceneResourceManager());
    m_scene_manager.sceneResourceManager().setDirty(false);
  }
}

/**********************************************************/
void VulkanRendererElement::onRender(const IRenderContext &ctx)
/**********************************************************/
{
  shaderio::PushConstant pushValues{};
  IRenderContext &ctx_ref = const_cast<IRenderContext &>(ctx);
  ctx_ref.pushValues = pushValues;
  ctx_ref.sceneResources = m_scene_manager.getScenePtr();
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
CameraPtr VulkanRendererElement::getCameraManipulator()
/**********************************************************/
{
  return m_scene_manager.camera();
}

/**********************************************************/
void VulkanRendererElement::onFileDrop(const std::filesystem::path &filename)
/**********************************************************/
{
  std::cout << "File dropped: " << filename << std::endl;
}

/**********************************************************/
void VulkanRendererElement::renderMaterials()
/**********************************************************/
{
  namespace PE = nvgui::PropertyEditor;
  auto &materials = m_scene_manager.sceneResourceManager().getMaterials();
  bool changed = false;

  if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
    for (size_t i = 0; i < materials.size(); i++) {
      std::string label = fmt::format("Material {}", i);
      if (ImGui::TreeNode(label.c_str())) {
        auto &mat = materials[i];
        PE::begin();

        changed |=
            PE::ColorEdit4("Base Color", glm::value_ptr(mat.baseColorFactor));
        changed |= PE::SliderFloat("Metallic", &mat.metallicFactor, 0.0f, 1.0f);
        changed |=
            PE::SliderFloat("Roughness", &mat.roughnessFactor, 0.0f, 1.0f);
        changed |= PE::ColorEdit3("Emission", glm::value_ptr(mat.emission));

        // Read-only info
        int texIdx = mat.baseColorTextureIndex;
        PE::Text("Texture Index", fmt::format("{}", texIdx).c_str());

        PE::end();
        ImGui::TreePop();
      }
    }
  }

  if (changed) {
    m_renderer->reset(); // Reset path tracing accumulation
    m_scene_manager.sceneResourceManager().onMaterialChange();
  }
}
/**********************************************************/
void VulkanRendererElement::renderInstances()
/**********************************************************/
{
  namespace PE = nvgui::PropertyEditor;
  auto &instances = m_scene_manager.sceneResourceManager().getInstances();
  const auto &materials = m_scene_manager.sceneResourceManager().getMaterials();
  const auto &shaderManager = m_renderer->getShaderManager();
  const auto &shaderRegistry = shaderManager.getRegistry();

  bool changed = false;

  if (ImGui::CollapsingHeader("Instances")) {
    for (size_t i = 0; i < instances.size(); i++) {
      std::string label = fmt::format("Instance {}", i);
      if (ImGui::TreeNode(label.c_str())) {
        auto &inst = instances[i];
        PE::begin();

        // 1. Material assignment
        int matIdx = static_cast<int>(inst.materialIndex);
        if (PE::SliderInt("Material Index", &matIdx, 0,
                          (int)materials.size() - 1)) {
          inst.materialIndex = (uint32_t)matIdx;
          changed = true;
        }

        // 2. Hit Group (Shader) assignment
        // Convert the current enum to an int for the UI
        int currentHitGroup = static_cast<int>(inst.hit_group);

        // Build a string list of available shader names from the registry
        std::string shaderNames;
        std::vector<MaterialType> types;
        for (auto const &[type, entry] : shaderRegistry) {
          shaderNames +=
              entry.entryPoint + '\0'; // Use the shader entry point name
          types.push_back(type);
        }
        shaderNames += '\0'; // End of list

        if (PE::Combo("Shader Type", &currentHitGroup, shaderNames.c_str(),
                      (int)types.size())) {
          // Update the instance with the selected enum type
          inst.hit_group = types[currentHitGroup];
          changed = true;
        }

        // 3. Show Read-only Mesh Info
        PE::Text("Mesh Index", fmt::format("{}", inst.meshIndex).c_str());

        // --- 1. Preparation: Convert Quat -> Euler (Degrees) ---
        glm::quat rotation = math::toQuat(glm::vec4(inst.rotation));
        glm::vec3 rotationEuler = glm::degrees(glm::eulerAngles(rotation));

        bool tChanged =
            PE::DragFloat3("Position", glm::value_ptr(inst.position), 0.1f);
        bool rChanged =
            PE::DragFloat3("Rotation", glm::value_ptr(rotationEuler), 0.1f);
        bool sChanged =
            PE::DragFloat3("Scale", glm::value_ptr(inst.scale), 0.1f);

        // --- 3. Write Back & Recompose ---
        if (tChanged || rChanged || sChanged) {
          glm::quat quat = glm::quat(glm::radians(rotationEuler));
          inst.rotation = math::fromQuat(quat);
          inst.transform =
              math::composeTransform(inst.position, inst.rotation, inst.scale);
          changed = true;
        }

        PE::end();
        ImGui::TreePop();
      }
    }
  }

  if (changed) {
    m_renderer->reset();
    m_scene_manager.sceneResourceManager().onInstanceChange();
    m_scene_manager.sceneResourceManager().setDirty(true);
  }
}
