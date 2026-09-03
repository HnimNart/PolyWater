/******************************************************************************
 * Copyright (c) 2024-2026, NVIDIA CORPORATION. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 *****************************************************************************/

#include "vulkan_imgui_system.hpp"

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <app/widgets/fonts.hpp>
#include <app/widgets/style.hpp>
#include <core/file_operations.hpp>

#include "app/app_info.hpp"
#include "backend/vulkan/core/vulkan_context_manager.hpp"
#include "backend/vulkan/core/vulkan_frame_synchronization_manager.hpp"
#include "backend/vulkan/core/vulkan_swapchain_render_manager.hpp"

namespace vkb
{

/**********************************************************/
VulkanImGuiSystem::~VulkanImGuiSystem()
/**********************************************************/
{
  deinit();
}

/**********************************************************/
void VulkanImGuiSystem::init(const app::ApplicationCreateInfo& info)
/**********************************************************/
{
  if (m_contextCreated)
  {
    return;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  m_contextCreated = true;

  setupImGui(info);
}

/**********************************************************/
void VulkanImGuiSystem::deinit()
/**********************************************************/
{
  saveSettings(m_iniFilename.c_str());
  shutdownVulkanBackend();
  destroyContext();
}

/**********************************************************/
void VulkanImGuiSystem::destroyContext()
/**********************************************************/
{
  if (!m_contextCreated)
  {
    return;
  }

  ImGui::DestroyContext();
  m_contextCreated = false;
}

/******************************************************************************
 * ImGui Setup
 *****************************************************************************/

/**********************************************************/
void VulkanImGuiSystem::setupImGui(const app::ApplicationCreateInfo& info)
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
void VulkanImGuiSystem::configureImGuiIO(const app::ApplicationCreateInfo& info)
/**********************************************************/
{
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags =
      ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

  if (info.headless)
  {
    io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
  }

  io.IniFilename = m_iniFilename.c_str();
}

/**********************************************************/
void VulkanImGuiSystem::initializeFonts()
/**********************************************************/
{
  ImGuiIO& io = ImGui::GetIO();

  app::addDefaultFont();
  io.FontDefault = app::getDefaultFont();
  app::addMonospaceFont();
}

/******************************************************************************
 * Vulkan Backend Initialization
 *****************************************************************************/

/**********************************************************/
void VulkanImGuiSystem::initVulkanBackend(VulkanContextManager& coreManager,
                                          uint maxFramesInFlight,
                                          VkFormat imageFormat,
                                          GLFWwindow* windowHandle)
/**********************************************************/
{
  if (m_vulkanInitialized)
  {
    return;
  }

  initializeGlfwBackend(windowHandle);
  initializeVulkanBackend(coreManager, maxFramesInFlight, imageFormat);

  m_vulkanInitialized = true;
}

/**********************************************************/
void VulkanImGuiSystem::initializeGlfwBackend(GLFWwindow* windowHandle)
/**********************************************************/
{
  if (windowHandle)
  {
    ImGui_ImplGlfw_InitForVulkan(windowHandle, true);
    m_glfwInitialized = true;
  }
}

/**********************************************************/
void VulkanImGuiSystem::initializeVulkanBackend(
    VulkanContextManager& coreManager, uint max_frames_in_flight,
    VkFormat _imageFormat)
/**********************************************************/
{
  static VkFormat imageFormat = _imageFormat;
  ImGui_ImplVulkan_InitInfo initInfo{
      .ApiVersion = VK_API_VERSION_1_4,
      .Instance = coreManager.getInstance(),
      .PhysicalDevice = coreManager.getPhysicalDevice(),
      .Device = coreManager.getDevice(),
      .QueueFamily = coreManager.getQueueInfo(0).familyIndex,
      .Queue = coreManager.getQueueInfo(0).queue,
      .DescriptorPool = coreManager.getDescriptorPool(),
      .MinImageCount = 2U,
      .ImageCount = max_frames_in_flight,
      .UseDynamicRendering = true,
      .PipelineRenderingCreateInfo =
          VkPipelineRenderingCreateInfo{
              .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
              .colorAttachmentCount = 1,
              .pColorAttachmentFormats = &imageFormat,
          },
  };

  ImGui_ImplVulkan_Init(&initInfo);
}

/**********************************************************/
void VulkanImGuiSystem::shutdownVulkanBackend()
/**********************************************************/
{
  if (!m_vulkanInitialized)
  {
    return;
  }
  ImGui_ImplVulkan_Shutdown();  // Optional
  if (m_glfwInitialized)
  {
    ImGui_ImplGlfw_Shutdown();
  }
  m_vulkanInitialized = false;
}

/******************************************************************************
 * Frame Operations
 *****************************************************************************/

/**********************************************************/
void VulkanImGuiSystem::beginFrame()
/**********************************************************/
{
  if (m_vulkanInitialized)
  {
    ImGui_ImplVulkan_NewFrame();
  }

  if (m_glfwInitialized)
  {
    ImGui_ImplGlfw_NewFrame();
  }
  ImGui::NewFrame();
}

/**********************************************************/
void VulkanImGuiSystem::endFrame()
/**********************************************************/
{
  ImGui::EndFrame();
  renderViewports();
}

/**********************************************************/
void VulkanImGuiSystem::render()
/**********************************************************/
{
  ImGui::Render();
}

/**********************************************************/
void VulkanImGuiSystem::renderViewports()
/**********************************************************/
{
  if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
  }
}

/**********************************************************/
void VulkanImGuiSystem::onRender(const IRenderContext& ctx)
/**********************************************************/
{
  const VulkanRenderContext& vkContext = VulkanRenderContext::get(ctx);
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkContext.cmdBuffer);
}

/******************************************************************************
 * Menu & Docking
 *****************************************************************************/

/**********************************************************/
void VulkanImGuiSystem::renderMenu(
    const std::vector<std::shared_ptr<app::IAppElement>>& elements)
/**********************************************************/
{
  setupImguiDock();

  if (ImGui::BeginMainMenuBar())
  {
    for (const auto& element : elements)
    {
      element->callOnUIMenu();
    }
    ImGui::EndMainMenuBar();
  }
}

/**********************************************************/
void VulkanImGuiSystem::setupImguiDock()
/**********************************************************/
{
  const ImGuiDockNodeFlags dockFlags =
      ImGuiDockNodeFlags_PassthruCentralNode |
      ImGuiDockNodeFlags_NoDockingInCentralNode;

  ImGuiID dockID =
      ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockFlags);

  if (!ImGui::DockBuilderGetNode(dockID)->IsSplitNode() &&
      !ImGui::FindWindowByName("Viewport"))
  {
    setupDefaultDockLayout(dockID);
  }
}

/**********************************************************/
void VulkanImGuiSystem::setupDefaultDockLayout(ImGuiID dockID)
/**********************************************************/
{
  ImGui::DockBuilderDockWindow("Viewport", dockID);
  ImGui::DockBuilderGetCentralNode(dockID)->LocalFlags |=
      ImGuiDockNodeFlags_NoTabBar;

  if (m_dockSetup)
  {
    m_dockSetup(dockID);
  }
  else
  {
    createDefaultLayout(dockID);
  }
}

/**********************************************************/
void VulkanImGuiSystem::createDefaultLayout(ImGuiID dockID)
/**********************************************************/
{
  ImGuiID leftID = ImGui::DockBuilderSplitNode(dockID, ImGuiDir_Left, 0.2f,
                                               nullptr, &dockID);
  ImGui::DockBuilderDockWindow("Settings", leftID);
}

/**********************************************************/
void VulkanImGuiSystem::setDockSetup(std::function<void(ImGuiID)> fn)
/**********************************************************/
{
  m_dockSetup = std::move(fn);
}

/**********************************************************/
void VulkanImGuiSystem::onDpiScaleChanged(float scaleRatio)
/**********************************************************/
{
  if (m_contextCreated)
  {
    ImGui::GetIO().FontGlobalScale *= scaleRatio;
  }
}

/******************************************************************************
 * Window Queries & Configuration
 *****************************************************************************/

/**********************************************************/
bool VulkanImGuiSystem::getWindowSize(const std::string& windowName,
                                      WindowSize& size)
/**********************************************************/
{
  const ImGuiWindow* viewport = ImGui::FindWindowByName(windowName.c_str());
  if (!viewport)
  {
    return false;
  }

  size = {uint32_t(viewport->Size.x), uint32_t(viewport->Size.y)};
  return true;
}

/**********************************************************/
void VulkanImGuiSystem::setWindowSize(const WindowSize& size)
/**********************************************************/
{
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize.x = float(size.width);
  io.DisplaySize.y = float(size.height);
}

/**********************************************************/
void VulkanImGuiSystem::setConfigFlags(unsigned int flags)
/**********************************************************/
{
  if (m_contextCreated)
  {
    ImGui::GetIO().ConfigFlags |= flags;
  }
}

/**********************************************************/
void VulkanImGuiSystem::loadSettings(const char* filename)
/**********************************************************/
{
  if (m_contextCreated)
  {
    ImGui::LoadIniSettingsFromDisk(filename);
  }
}

/**********************************************************/
void VulkanImGuiSystem::saveSettings(const char* filename)
/**********************************************************/
{
  if (m_contextCreated)
  {
    ImGui::SaveIniSettingsToDisk(filename);
  }
}

}  // namespace vkb
