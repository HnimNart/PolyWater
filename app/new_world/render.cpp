#include "render.hpp"

// Standard Libs
#include <cstdio>

// Third Party
#include <imgui/imgui.h>
#include <tinygltf/tiny_gltf.h>
#include <vulkan/vulkan.h>

// Project Headers
#include <shaders/shaderio.h>

#include <nvgui/property_editor.hpp>

#include "backend/vulkan/VulkanSceneRenderer.hpp"
#include "common/path_utils.hpp"
#include "core/application/App.hpp"

void RtBasic::setup_scene(VkCommandBuffer cmd)
{
  SCOPED_TIMER(__FUNCTION__);
  SceneResources& scene_resources = m_scene_manager.scene_resources();

  // Load the GLTF resources
  // Note: Ensure headers for nvutils and nvsamples are included if they aren't in path_utils.hpp
  tinygltf::Model teapotModel =
      scene_resources.loadGltf(nvutils::findFile("teapot.gltf", nvsamples::getResourcesDirs()));

  tinygltf::Model planeModel =
      scene_resources.loadGltf(nvutils::findFile("plane.gltf", nvsamples::getResourcesDirs()));
}

void RtBasic::onAttach(core::Application* app)
{
  m_app = app;  // Store app pointer if needed, though not strictly used in snippet
  auto* backend = dynamic_cast<core::VulkanBackend*>(app->get_backend());

  if (backend)
  {
    m_renderer = std::make_shared<VulkanSceneRenderer>(backend);
    m_scene_manager = SceneManager(m_renderer);
  }
  else
  {
    printf("Error: Backend is not VulkanBackend\n");
  }
}

void RtBasic::onDetach()
{
  // Cleanup if necessary
}

void RtBasic::onResize(WindowSize size)
{
  // Handle resize
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
    // ImGui::Image(
    //      ImTextureID(m_renderer->gbuffers().getDescriptorSet(eImgTonemapped)),
    //      ImGui::GetContentRegionAvail());
  }
  ImGui::End();
}

void RtBasic::onPreRender()
{
  printf("PreRender\n");
}

void RtBasic::onRender(FrameContext* ctx)
{
  printf("render\n");
}

void RtBasic::onEndFrame(const FrameContext& frame)
{
  printf("onEndFrame\n");
}

CameraPtr RtBasic::getCameraManipulator() const
{
  return m_cameraManip;
}
