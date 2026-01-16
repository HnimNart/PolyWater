#include "render.hpp"

// Standard Libs
#include <cstdio>

// Third Party
#include <imgui/imgui.h>
#include <tinygltf/tiny_gltf.h>
#include <vulkan/vulkan.h>

// Project Headers
#include <shaders/shaderio.h>

#include <nvgui/camera.hpp>
#include <nvgui/property_editor.hpp>
#include <nvgui/tonemapper.hpp>  // Tonemapper widget

#include "backend/vulkan/VulkanSceneRenderer.hpp"
#include "common/path_utils.hpp"
#include "common/timers.hpp"
#include "core/application/App.hpp"
#include "scene/Shared.hpp"

void RtBasic::setup_scene()
{
  common::ScopedTimer(__FUNCTION__);
  SceneResources& scene_resources = m_scene_manager.scene_resources();
  scene_resources.begin_uploading();

  // Load the GLTF resources
  // Note: Ensure headers for nvutils and nvsamples are included if they aren't in path_utils.hpp
  tinygltf::Model teapotModel =
      scene_resources.loadGltf(nvutils::findFile("teapot.gltf", nvsamples::getResourcesDirs()));

  tinygltf::Model planeModel =
      scene_resources.loadGltf(nvutils::findFile("plane.gltf", nvsamples::getResourcesDirs()));

  // Textures
  {
    scene_resources.loadTexture(
        nvutils::findFile("tiled_floor.png", nvsamples::getResourcesDirs()));
  }

  // Teapot material
  SceneResources::MaterialID teapot_id =
      scene_resources.addMaterial({.baseColorFactor = glm::vec4(0.8f, 1.0f, 0.6f, 1.0f),
                                   .metallicFactor = 0.5f,
                                   .roughnessFactor = 0.5f});
  // Plane material with texture
  SceneResources::MaterialID plane_id =
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
  nvsamples::GltfSceneResource& gltf_resources = m_scene_manager.gltf_resources();
  shaderio::GltfSceneInfo& sceneInfo = gltf_resources.sceneInfo;
  sceneInfo.useSky = false;  // Use light
  sceneInfo.instances = (shaderio::GltfInstance*)
                            gltf_resources.bInstances.address;  // Address of the instance buffer
  sceneInfo.meshes =
      (shaderio::GltfMesh*) gltf_resources.bMeshes.address;  // Address of the mesh buffer
  sceneInfo.materials = (shaderio::GltfMetallicRoughness*)
                            gltf_resources.bMaterials.address;  // Address of the material buffer
  sceneInfo.backgroundColor = {0.85f, 0.85f, 0.85f};            // The background color
  sceneInfo.numLights = 1;
  sceneInfo.punctualLights[0].color = glm::vec3(1.0f, 1.0f, 1.0f);
  sceneInfo.punctualLights[0].intensity = 4.0f;
  sceneInfo.punctualLights[0].position = glm::vec3(1.0f, 1.0f, 1.0f);   // Position of the light
  sceneInfo.punctualLights[0].direction = glm::vec3(1.0f, 1.0f, 1.0f);  // Direction to the light
  sceneInfo.punctualLights[0].type = shaderio::GltfLightType::ePoint;
  sceneInfo.punctualLights[0].coneAngle =
      0.9f;  // Cone angle for spot lights (0 for point and directional lights)

  // Default camera
  m_cameraManip->setClipPlanes({0.01F, 100.0F});
  m_cameraManip->setLookat({0.0F, 0.5F, 5.0}, {0.F, 0.F, 0.F}, {0.0F, 1.0F, 0.0F});

  scene_resources.end_uploading();

  // Now build scene
  m_scene_manager.postInit();
}

void RtBasic::onAttach(core::Application* app)
{
  m_app = app;
  auto* backend = dynamic_cast<core::VulkanBackend*>(app->get_backend());
  assert(backend && "Backend is not VulkanBackend");
  m_renderer = std::make_shared<VulkanSceneRenderer>(backend);
  m_scene_manager = SceneManager(m_renderer);
  m_scene_manager.set_camera(m_cameraManip);

  setup_scene();
}

void RtBasic::onDetach()
{
  // Cleanup if necessary
}

void RtBasic::onResize(WindowSize size)
{
  m_scene_manager.onResize({size.width, size.height});
}

void RtBasic::onUIMenu()
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
    // vkQueueWaitIdle(m_app->getQueue(0).queue);
    // m_scene_manager->reload(m_useRayTracing);
  }
}

void RtBasic::onUIRender()
{
  namespace PE = nvgui::PropertyEditor;

  // Display the rendering GBuffer in the ImGui window ("Viewport")
  if (ImGui::Begin("Viewport"))
  {
    ImGui::Image(ImTextureID(m_renderer->gbuffers().getDescriptorSet(eImgTonemapped)),
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
    // if (ImGui::CollapsingHeader("Environment"))
    // {
    //   ImGui::Checkbox("Use Sky", (bool*) &m_scene_manager->gltf_resources().sceneInfo.useSky);
    //   if (m_scene_manager->gltf_resources().sceneInfo.useSky)
    //     nvgui::skySimpleParametersUI(m_scene_manager->gltf_resources().sceneInfo.skySimpleParam);
    //   else
    //   {
    //     PE::begin();
    //     PE::ColorEdit3("Background",
    //                    (float*) &m_scene_manager->gltf_resources().sceneInfo.backgroundColor);
    //     PE::end();
    //     // Light
    //     PE::begin();
    //     if (m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].type ==
    //             shaderio::GltfLightType::ePoint ||
    //         m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].type ==
    //             shaderio::GltfLightType::eSpot)
    //     {
    //       PE::DragFloat3(
    //           "Light Position",
    //           glm::value_ptr(
    //               m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].position),
    //           1.0f, -20.0f, 20.0f, "%.2f", ImGuiSliderFlags_None, "Position of the light");
    //     }
    //     if (m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].type ==
    //             shaderio::GltfLightType::eDirectional ||
    //         m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].type ==
    //             shaderio::GltfLightType::eSpot)
    //     {
    //       PE::SliderFloat3(
    //           "Light Direction",
    //           glm::value_ptr(
    //               m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].direction),
    //           -1.0f, 1.0f, "%.2f", ImGuiSliderFlags_None, "Direction of the light");
    //     }

    //     PE::SliderFloat("Light Intensity",
    //                     &m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].intensity,
    //                     0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_Logarithmic,
    //                     "Intensity of the light");
    //     PE::ColorEdit3(
    //         "Light Color",
    //         glm::value_ptr(m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].color),
    //         ImGuiColorEditFlags_NoInputs, "Color of the light");
    //     PE::Combo("Light Type",
    //               (int*) &m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].type,
    //               "Point\0Spot\0Directional\0", 3, "Type of the light (Point, Spot,
    //               Directional)");
    //     if (m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].type ==
    //         shaderio::GltfLightType::eSpot)
    //     {
    //       PE::SliderAngle("Cone Angle",
    //                       &m_scene_manager->gltf_resources().sceneInfo.punctualLights[0].coneAngle,
    //                       0.f, 90.f, "%.2f", ImGuiSliderFlags_AlwaysClamp,
    //                       "Cone angle of the spot light");
    //     }
    //     PE::end();
    //   }
    // }
    if (ImGui::CollapsingHeader("Tonemapper"))
    {
      nvgui::tonemapperWidget(m_scene_manager.tonemapper());
    }
  }
  ImGui::End();
}

void RtBasic::onPreRender()
{
}

void RtBasic::onRender(FrameContext* ctx)
{
  m_scene_manager.render(m_useRayTracing);
}

void RtBasic::onEndFrame(const FrameContext* frame)
{
  m_scene_manager.post_process();
}

CameraPtr& RtBasic::getCameraManipulator()
{
  return m_cameraManip;
}
