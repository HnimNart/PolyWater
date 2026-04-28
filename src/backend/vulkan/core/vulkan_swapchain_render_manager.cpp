#include "vulkan_swapchain_render_manager.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#undef APIENTRY
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include "vulkan_context_manager.hpp"
#include "vulkan_frame_synchronization_manager.hpp"
#include "nvvk/check_error.hpp"
#include "nvvk/debug_util.hpp"

/**********************************************************/
void VulkanSwapchainRenderManager::init(VulkanContextManager& coreManager,
                                  GLFWwindow* windowHandle)
/**********************************************************/
{
  if (!windowHandle)
  {
    throw std::runtime_error(
        "VulkanSwapchainRenderManager initialized without a valid GLFW window.");
  }

  VkDevice device = coreManager.getDevice();
  VkPhysicalDevice physicalDevice = coreManager.getPhysicalDevice();
  VkInstance instance = coreManager.getInstance();
  const nvvk::QueueInfo& graphicsQueue = coreManager.getQueueInfo(0);

  // Create Window Surface
  NVVK_CHECK(
      glfwCreateWindowSurface(instance, windowHandle, nullptr, &m_surface));

  // Create Swapchain
  nvvk::Swapchain::InitInfo swapChainInit{
      .physicalDevice = physicalDevice,
      .device = device,
      .queue = graphicsQueue,
      .surface = m_surface,
      .cmdPool = coreManager.getTransientCmdPool(),
      .preferredVsyncOffMode = VK_PRESENT_MODE_MAX_ENUM_KHR,
      .preferredVsyncOnMode = VK_PRESENT_MODE_MAX_ENUM_KHR,
  };

  const VkResult result = m_swapchain.init(swapChainInit);
  if (result != VK_SUCCESS)
  {
    reportSwapchainDiagnostics(instance, swapChainInit);
    nvvk::CheckError::getInstance().check(result, "m_swapchain.init", __FILE__,
                                          __LINE__);
  }

  // Initialize Swapchain Resources
  NVVK_CHECK(m_swapchain.initResources(m_windowSize, m_vsyncWanted));
}

/**********************************************************/
bool VulkanSwapchainRenderManager::beginFrame(VulkanContextManager& coreManager)
/**********************************************************/
{
  if (m_swapchain.needRebuilding())
  {
    NVVK_CHECK(m_swapchain.reinitResources(m_windowSize, m_vsyncWanted));
  }

  // acquire the next image index and signals the 'imageAvailable' semaphore
  VkResult res = m_swapchain.acquireNextImage(coreManager.getDevice());
  if (!(res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR))
  {
    return false;
  }

  return true;
}

/**********************************************************/
void VulkanSwapchainRenderManager::present(VulkanContextManager& coreManager)
/**********************************************************/
{
  m_swapchain.presentFrame(coreManager.getQueueInfo(0).queue);
}

/**********************************************************/
void VulkanSwapchainRenderManager::setVsync(bool enabled)
/**********************************************************/
{
  m_vsyncWanted = enabled;
  m_swapchain.requestRebuild();
}

/**********************************************************/
void VulkanSwapchainRenderManager::deinit(VulkanContextManager& coreManager)
/**********************************************************/
{
  VkDevice device = coreManager.getDevice();
  NVVK_CHECK(vkDeviceWaitIdle(device));
  m_swapchain.deinit();
  vkDestroySurfaceKHR(coreManager.getInstance(), m_surface, nullptr);
}

/**********************************************************/
void VulkanSwapchainRenderManager::setUICallback(const RenderCallback& renderCallback)
/**********************************************************/
{
  m_uiCallback = renderCallback;
}

/**********************************************************/
VkImage VulkanSwapchainRenderManager::getOutputImage() const
/**********************************************************/
{
  return m_swapchain.getImage();
}

/**********************************************************/
VkImageView VulkanSwapchainRenderManager::getOutputImageView() const
/**********************************************************/
{
  return m_swapchain.getImageView();
}

/**********************************************************/
void VulkanSwapchainRenderManager::reportSwapchainDiagnostics(
    VkInstance instance, nvvk::Swapchain::InitInfo& swapchainParams)
/**********************************************************/
{
  LOGI("\nAvailable GPUs and presentation support for surface %p:\n",
       swapchainParams.surface);
  uint32_t gpuCount = 0;
  std::vector<VkPhysicalDevice> gpus;
  if (instance == nullptr || swapchainParams.surface == VK_NULL_HANDLE)
  {
    LOGI("  <instance or surface was nullptr>\n");
  }
  else if (VK_SUCCESS !=
           vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr))
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

      uint32_t queueFamilyCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(gpus[gpuIdx], &queueFamilyCount,
                                               nullptr);
      bool anyCanPresent = false;
      std::vector<uint32_t> presentableQueueFamilies;
      for (uint32_t queueFamilyIdx = 0; queueFamilyIdx < queueFamilyCount;
           queueFamilyIdx++)
      {
        VkBool32 presentSupported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpus[gpuIdx], queueFamilyIdx,
                                             swapchainParams.surface,
                                             &presentSupported);
        if (VK_TRUE == presentSupported)
        {
          anyCanPresent = true;
          presentableQueueFamilies.push_back(queueFamilyIdx);
        }
      }
    }
  }

  VkPhysicalDeviceProperties chosenDeviceProps{};
  vkGetPhysicalDeviceProperties(swapchainParams.physicalDevice,
                                &chosenDeviceProps);
  LOGE("Failed to create the swapchain for VkSurface %p with VkPhysicalDevice "
       "%p (%s).\n",
       swapchainParams.surface, swapchainParams.physicalDevice,
       chosenDeviceProps.deviceName);
}
