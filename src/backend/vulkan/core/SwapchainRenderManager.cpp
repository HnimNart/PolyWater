#include "SwapchainRenderManager.hpp"

#include <GLFW/glfw3.h>
#undef APIENTRY
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/helpers.hpp>

#include "CoreManager.hpp"
#include "FrameSynchronizationManager.hpp"
#include "Utils.hpp"

void SwapchainRenderManager::init(VulkanCoreManager& coreManager,
                                  FrameSynchronizationManager& frameSyncManager,
                                  GLFWwindow* windowHandle,
                                  const core::ApplicationCreateInfo& appInfo)
{
  m_windowHandle = windowHandle;
  m_headless = appInfo.headless;

  if (!m_headless && !m_windowHandle)
  {
    throw std::runtime_error("SwapchainRenderManager initialized without a valid GLFW window.");
  }

  VkDevice device = coreManager.getDevice();
  VkPhysicalDevice physicalDevice = coreManager.getPhysicalDevice();
  VkInstance instance = coreManager.getInstance();
  const nvvk::QueueInfo& graphicsQueue = coreManager.getQueueInfo(0);

  if (!m_headless)
  {
    // Create Window Surface
    NVVK_CHECK(glfwCreateWindowSurface(instance, m_windowHandle, nullptr, &m_surface));

    // Create Swapchain
    nvvk::Swapchain::InitInfo swapChainInit{
        .physicalDevice = physicalDevice,
        .device = device,
        .surface = m_surface,
        .queue = graphicsQueue,
        .cmdPool = coreManager.getTransientCmdPool(),
        .preferredVsyncOffMode = VK_PRESENT_MODE_MAX_ENUM_KHR,
        .preferredVsyncOnMode = VK_PRESENT_MODE_MAX_ENUM_KHR,
    };

    const VkResult result = m_swapchain.init(swapChainInit);
    if (result != VK_SUCCESS)
    {
      reportSwapchainDiagnostics(instance, swapChainInit);
      nvvk::CheckError::getInstance().check(result, "m_swapchain.init", __FILE__, __LINE__);
    }

    // Initialize Swapchain Resources
    NVVK_CHECK(m_swapchain.initResources(m_windowSize, m_vsyncWanted));

    setupImGuiVulkanBackend(coreManager, frameSyncManager.getFrameCycleSize());
  }
}

void SwapchainRenderManager::setupImGuiVulkanBackend(VulkanCoreManager& coreManager,
                                                     uint32_t framesInFlight)
{
  static VkFormat imageFormats = VK_FORMAT_B8G8R8A8_UNORM;

  if (!m_headless)
  {
    ImGui_ImplGlfw_InitForVulkan(m_windowHandle, true);
    imageFormats = m_swapchain.getImageFormat();
  }

  ImGui_ImplVulkan_InitInfo initInfo = {
      .ApiVersion = VK_API_VERSION_1_4,
      .Instance = coreManager.getInstance(),
      .PhysicalDevice = coreManager.getPhysicalDevice(),
      .Device = coreManager.getDevice(),
      .QueueFamily = coreManager.getQueueInfo(0).familyIndex,
      .Queue = coreManager.getQueueInfo(0).queue,
      .DescriptorPool = coreManager.getDescriptorPool(),
      .MinImageCount = 2U,
      .ImageCount = std::max(framesInFlight, 2U),
      .UseDynamicRendering = true,
      .PipelineRenderingCreateInfo =
          {
              .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
              .colorAttachmentCount = 1,
              .pColorAttachmentFormats = &imageFormats,
          },
  };
  ImGui_ImplVulkan_Init(&initInfo);
}

bool SwapchainRenderManager::beginFrame(VulkanCoreManager& coreManager)
{
  if (!m_headless && m_swapchain.needRebuilding())
  {
    NVVK_CHECK(m_swapchain.reinitResources(m_windowSize, m_vsyncWanted));
  }

  if (!m_headless)
  {
    VkResult res = m_swapchain.acquireNextImage(coreManager.getDevice());
    if (!(res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR))
    {
      return false;
    }
  }

  return true;
}

void SwapchainRenderManager::renderToSwapchain(VkCommandBuffer cmd)
{
  if (m_headless)
  {
    return;
  }

  beginDynamicRenderingToSwapchain(cmd);
  {
    nvvk::DebugUtil::ScopedCmdLabel scopedCmdLabel(cmd, "ImGui");
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
  }
  endDynamicRenderingToSwapchain(cmd);
}

void SwapchainRenderManager::beginDynamicRenderingToSwapchain(VkCommandBuffer cmd) const
{
  const VkRenderingAttachmentInfo colorAttachment{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = m_swapchain.getImageView(),
      .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}},
  };

  const VkRenderingInfo renderingInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {{0, 0}, {m_windowSize.width, m_windowSize.height}},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &colorAttachment,
  };

  nvvk::cmdImageMemoryBarrier(cmd, {m_swapchain.getImage(), VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});

  vkCmdBeginRendering(cmd, &renderingInfo);
}

void SwapchainRenderManager::endDynamicRenderingToSwapchain(VkCommandBuffer cmd) const
{
  vkCmdEndRendering(cmd);

  nvvk::cmdImageMemoryBarrier(cmd,
                              {m_swapchain.getImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR});
}

void SwapchainRenderManager::present(VulkanCoreManager& coreManager)
{
  if (!m_headless)
  {
    m_swapchain.presentFrame(coreManager.getQueueInfo(0).queue);
  }
}

void SwapchainRenderManager::setVsync(bool enabled)
{
  m_vsyncWanted = enabled;
  if (!m_headless)
  {
    m_swapchain.requestRebuild();
  }
}

void SwapchainRenderManager::deinit(VulkanCoreManager& coreManager)
{
  VkDevice device = coreManager.getDevice();
  NVVK_CHECK(vkDeviceWaitIdle(device));

  ImGui_ImplVulkan_Shutdown();

  if (!m_headless)
  {
    m_swapchain.deinit();
    vkDestroySurfaceKHR(coreManager.getInstance(), m_surface, nullptr);
  }
}

// Provides additional diagnostic information about which GPUs can be used with
// the given VkSurface. Only used when handling errors.
void SwapchainRenderManager::reportSwapchainDiagnostics(VkInstance instance,
                                                        nvvk::Swapchain::InitInfo& swapchainParams)
{
  LOGI("\nAvailable GPUs and presentation support for surface %p:\n", swapchainParams.surface);
  uint32_t gpuCount = 0;
  std::vector<VkPhysicalDevice> gpus;
  if (instance == nullptr || swapchainParams.surface == VK_NULL_HANDLE)
  {
    LOGI("  <instance or surface was nullptr>\n");
  }
  else if (VK_SUCCESS != vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr))
  {
    LOGI("  <vkEnumeratePhysicalDevices failed>\n");
  }
  else if (0 == gpuCount)
  {
    LOGI("  <no devices>\n");
  }
  else
  {
    gpus.resize(gpuCount);
    vkEnumeratePhysicalDevices(instance, &gpuCount, gpus.data());
    for (uint32_t gpuIdx = 0; gpuIdx < gpuCount; gpuIdx++)
    {
      VkPhysicalDeviceProperties deviceProps{};
      vkGetPhysicalDeviceProperties(gpus[gpuIdx], &deviceProps);

      // Check which queue families on this GPU can present
      uint32_t queueFamilyCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(gpus[gpuIdx], &queueFamilyCount, nullptr);
      bool anyCanPresent = false;
      std::vector<uint32_t> presentableQueueFamilies;
      for (uint32_t queueFamilyIdx = 0; queueFamilyIdx < queueFamilyCount; queueFamilyIdx++)
      {
        VkBool32 presentSupported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpus[gpuIdx], queueFamilyIdx, swapchainParams.surface,
                                             &presentSupported);
        if (VK_TRUE == presentSupported)
        {
          anyCanPresent = true;
          presentableQueueFamilies.push_back(queueFamilyIdx);
        }
      }

      // TODO fix this include
      if (anyCanPresent)
      {
        // PRINTI("  GPU {} ({}): CAN present (using queue family indices {})\n", gpuIdx,
        //        deviceProps.deviceName, presentableQueueFamilies);
      }
      else
      {
        // PRINTI("  GPU {} ({}): CANNOT present\n", gpuIdx, deviceProps.deviceName);
      }
    }
  }

  VkPhysicalDeviceProperties chosenDeviceProps{};
  vkGetPhysicalDeviceProperties(swapchainParams.physicalDevice, &chosenDeviceProps);
  LOGE("Failed to create the swapchain for VkSurface %p with VkPhysicalDevice %p (%s).\n"
       "This might happen if you're on a multi-monitor Linux system with different GPUs plugged "
       "into different windowing system desktops, and GLFW chose a desktop not connected to the "
       "physical device that the sample or nvvk::Context chose.\n"
       "To fix this, set nvvk::ContextInfo in the sample to the index of a GPU with \"CAN "
       "Present\" listed next to it above.\n",
       swapchainParams.surface, swapchainParams.physicalDevice, chosenDeviceProps.deviceName);
  // Note that this is essentially a workaround for a bug that would require
  // changing the nvpro_core2 design; to fix this, we would either need to create
  // the window and surface before the context, or we would need to link NVVK
  // against GLFW and have nvvk::Context call glfwGetPhysicalDevicePresentationSupport.
}
