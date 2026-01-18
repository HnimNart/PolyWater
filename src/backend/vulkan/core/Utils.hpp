#pragma once

#include <volk.h>
#include <vulkan/vulkan_core.h>

#include <nvutils/logger.hpp>
#include <nvutils/timers.hpp>
#include <nvvk/context.hpp>
#include <nvvk/default_structs.hpp>
#include <nvvk/swapchain.hpp>
#include <nvvk/validation_settings.hpp>

// Provides additional diagnostic information about which GPUs can be used with
// the given VkSurface. Only used when handling errors.
void reportSwapchainDiagnostics(VkInstance instance, nvvk::Swapchain::InitInfo& swapchainParams)
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
