#include "VulkanRenderElement.hpp"

// Standard Libs
#include <cstdio>

// Third Party
#include <imgui/imgui.h>
#include <shaders/shaderio.h>
#include <tinygltf/tiny_gltf.h>
#include <vulkan/vulkan.h>

#include <glm/gtc/type_ptr.hpp>
#include <nvgui/camera.hpp>
#include <nvgui/property_editor.hpp>
#include <nvgui/sky.hpp>
#include <nvgui/tonemapper.hpp>

#include "backend/vulkan/render/Renderer.hpp"
#include "common/path_utils.hpp"
#include "common/timers.hpp"
#include "core/application/App.hpp"

void VulkanRendererElement::setupScene()
{
  common::ScopedTimer(__FUNCTION__);
  SceneResourcesManager& scene_resources = m_scene_manager.sceneResources();
  scene_resources.beginUploading();

  // Load the GLTF resources
  tinygltf::Model teapotModel =
      scene_resources.loadGltf(nvutils::findFile("teapot.gltf", common::getResourcesDirs()));

  tinygltf::Model planeModel =
      scene_resources.loadGltf(nvutils::findFile("plane.gltf", common::getResourcesDirs()));

  // Textures
  {
    scene_resources.loadTexture(nvutils::findFile("tiled_floor.png", common::getResourcesDirs()));
  }

  // Teapot material
  SceneResourcesManager::MaterialID teapot_id =
      scene_resources.addMaterial({.baseColorFactor = glm::vec4(0.8f, 1.0f, 0.6f, 1.0f),
                                   .metallicFactor = 0.5f,
                                   .roughnessFactor = 0.5f});
  // Plane material with texture
  SceneResourcesManager::MaterialID plane_id =
      scene_resources.addMaterial({.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                                   .metallicFactor = 0.1f,
                                   .roughnessFactor = 0.8f,
                                   .baseColorTextureIndex = 1});

  // Teapot
  scene_resources.addInstance({.transform = glm::translate(glm::mat4(1), glm::vec3(0, 0, 0)) *
                                            glm::scale(glm::mat4(1), glm::vec3(0.5f)),
                               .materialIndex = teapot_id,
                               .meshIndex = 0});
  // Plane
  scene_resources.addInstance(
      {.transform =
           glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, -0.9f, 0)), glm::vec3(2.f)),
       .materialIndex = plane_id,
       .meshIndex = 1});
  scene_resources.finalizeSceneResources();

  // Scene information
  shaderio::GltfSceneInfo& sceneInfo = m_scene_manager.sceneInfo();
  sceneInfo.useSky = false;                           // Use light
  sceneInfo.backgroundColor = {0.85f, 0.85f, 0.85f};  // The background color
  sceneInfo.numLights = 1;
  sceneInfo.punctualLights[0].color = glm::vec3(1.0f, 1.0f, 1.0f);
  sceneInfo.punctualLights[0].intensity = 4.0f;
  sceneInfo.punctualLights[0].position = glm::vec3(1.0f, 1.0f, 1.0f);   // Position of the light
  sceneInfo.punctualLights[0].direction = glm::vec3(1.0f, 1.0f, 1.0f);  // Direction to the light
  sceneInfo.punctualLights[0].type = shaderio::GltfLightType::ePoint;
  sceneInfo.punctualLights[0].coneAngle =
      0.9f;  // Cone angle for spot lights (0 for point and directional lights)

  // Default camera
  m_scene_manager.camera()->setClipPlanes({0.01F, 100.0F});
  m_scene_manager.camera()->setLookat({0.0F, 0.5F, 5.0}, {0.F, 0.F, 0.F}, {0.0F, 1.0F, 0.0F});

  // Finish uploading command
  scene_resources.endUploading();
  // build scene
  m_scene_manager.postInit();
}

void VulkanRendererElement::onAttach(core::Application* app)
{
  m_app = app;
  auto* backend = dynamic_cast<VulkanBackend*>(app->getBackend());
  assert(backend && "Backend is not VulkanBackend");
  m_renderer = std::make_shared<VulkanRenderer>(backend);
  m_scene_manager = SceneManager(m_renderer);
  setupScene();
}

void VulkanRendererElement::onDetach()
{
  m_scene_manager.clear();
}

void VulkanRendererElement::onResize(WindowSize size)
{
  m_scene_manager.onResize(size);
}

void VulkanRendererElement::onUIMenu()
{
  bool reload = false;
  if (ImGui::BeginMenu("Tools"))
  {
    reload |= ImGui::MenuItem("Reload Shaders", "F5");
    ImGui::EndMenu();
  }
  reload |= ImGui::IsKeyPressed(ImGuiKey_F5);

  if (reload)
  {
    m_scene_manager.reload(m_useRayTracing);
  }
}

void VulkanRendererElement::onUIRender()
{
  namespace PE = nvgui::PropertyEditor;

  // Display the rendering GBuffer in the ImGui window ("Viewport")
  if (ImGui::Begin("Viewport"))
  {
    ImGui::Image(ImTextureID(m_scene_manager.getTonemapedImageDescriptor()),
                 ImGui::GetContentRegionAvail());
  }
  ImGui::End();

  // Setting panel
  if (ImGui::Begin("Settings"))
  {
    // Ray tracing toggle
    ImGui::Checkbox("Use Ray Tracing", &m_useRayTracing);
    if (ImGui::CollapsingHeader("Camera"))
    {
      nvgui::CameraWidget(m_scene_manager.camera());
    }
    if (ImGui::CollapsingHeader("Environment"))
    {
      // Capture by reference so ImGui updates the original data
      auto& sceneInfo = m_scene_manager.sceneInfo();

      ImGui::Checkbox("Use Sky", (bool*) &sceneInfo.useSky);
      if (sceneInfo.useSky)
      {
        nvgui::skySimpleParametersUI(sceneInfo.skySimpleParam);
      }
      else
      {
        PE::begin();
        PE::ColorEdit3("Background", (float*) &sceneInfo.backgroundColor);
        PE::end();

        // Light - Reference the first light for clarity
        auto& light = sceneInfo.punctualLights[0];

        PE::begin();
        if (light.type == shaderio::GltfLightType::ePoint ||
            light.type == shaderio::GltfLightType::eSpot)
        {
          PE::DragFloat3("Light Position", glm::value_ptr(light.position), 1.0f, -20.0f, 20.0f,
                         "%.2f", ImGuiSliderFlags_None, "Position of the light");
        }
        if (light.type == shaderio::GltfLightType::eDirectional ||
            light.type == shaderio::GltfLightType::eSpot)
        {
          PE::SliderFloat3("Light Direction", glm::value_ptr(light.direction), -1.0f, 1.0f, "%.2f",
                           ImGuiSliderFlags_None, "Direction of the light");
        }

        PE::SliderFloat("Light Intensity", &light.intensity, 0.0f, 1000.0f, "%.2f",
                        ImGuiSliderFlags_Logarithmic, "Intensity of the light");

        PE::ColorEdit3("Light Color", glm::value_ptr(light.color), ImGuiColorEditFlags_NoInputs,
                       "Color of the light");

        PE::Combo("Light Type", (int*) &light.type, "Point\0Spot\0Directional\0", 3,
                  "Type of the light (Point, Spot, Directional) ");

        if (light.type == shaderio::GltfLightType::eSpot)
        {
          PE::SliderAngle("Cone Angle", &light.coneAngle, 0.f, 90.f, "%.2f",
                          ImGuiSliderFlags_AlwaysClamp, "Cone angle of the spot light");
        }
        PE::end();
      }
    }
    if (ImGui::CollapsingHeader("Tonemapper"))
    {
      nvgui::tonemapperWidget(m_scene_manager.tonemapper());
    }
  }
  ImGui::End();
}

void VulkanRendererElement::onPreRender()
{
}

void VulkanRendererElement::onRender(const IRenderContext* /*ctx */)
{
  m_scene_manager.render(m_useRayTracing);
}

void VulkanRendererElement::onEndFrame(const IRenderContext* /*frame*/)
{
  m_scene_manager.postProcess();
}

void VulkanRendererElement::onLastHeadlessFrame()
{
  m_renderer->saveImage(nvutils::getExecutablePath().replace_extension(".jpg").string());
}

CameraPtr VulkanRendererElement::getCameraManipulator()
{
  return m_scene_manager.camera();
}

void VulkanRendererElement::onFileDrop(const std::filesystem::path& filename)
{
  std::cout << filename << std::endl;
}
