/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "vk_utils.hpp"

#include <stb/stb_image.h>
#include <volk.h>

#include <nvapp/application.hpp>
#include <nvutils/file_operations.hpp>
#include <nvutils/logger.hpp>
#include <nvutils/timers.hpp>
#include <nvvk/check_error.hpp>
#include <nvvk/context.hpp>
#include <nvvk/default_structs.hpp>
#include <nvvk/staging.hpp>
#include <nvvk/swapchain.hpp>
#include <nvvk/validation_settings.hpp>

#include "core/application/AppInfo.hpp"

namespace vk_utils
{

nvvk::Image loadAndCreateImage(VkCommandBuffer cmd, nvvk::StagingUploader& staging, VkDevice device,
                               const std::filesystem::path& filename, bool sRgb)
{
  // Load the image from disk
  int w, h, comp, req_comp{4};
  std::string filenameUtf8 = nvutils::utf8FromPath(filename);
  const stbi_uc* data = stbi_load(filenameUtf8.c_str(), &w, &h, &comp, req_comp);
  assert((data != nullptr) && "Could not load texture image!");

  // Define how to create the image
  VkImageCreateInfo imageInfo = DEFAULT_VkImageCreateInfo;
  imageInfo.format = sRgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.extent = {uint32_t(w), uint32_t(h), 1};
  imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

  nvvk::ResourceAllocator* allocator = staging.getResourceAllocator();

  // Use the VMA allocator to create the image
  const std::span dataSpan(data, w * h * req_comp);
  nvvk::Image texture;
  NVVK_CHECK(allocator->createImage(texture, imageInfo, DEFAULT_VkImageViewCreateInfo));
  // texture.descriptor.imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  NVVK_CHECK(staging.appendImage(texture, dataSpan, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

  return texture;
}

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

template <typename T> nvvk::ContextInitInfo createVkContextInfo(const T& appInfo)
{

  // Setting up the Vulkan context, instance and device extensions
  VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjectFeatures{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT};

  // Add ray tracing features
  VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};

  nvvk::ContextInitInfo vkSetup{
      .instanceExtensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME},
      .deviceExtensions =
          {
              {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME},
              {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjectFeatures},
              {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
               &accelFeature},  // Build acceleration structures
              {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
               &rtPipelineFeature},                              // Use vkCmdTraceRaysKHR
              {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME},  // Required by ray tracing pipeline
          },
  };
  if (!appInfo.headless)
  {
    nvvk::addSurfaceExtensions(vkSetup.instanceExtensions, &vkSetup.deviceExtensions);
  }

  // Adding control on the validation layers
  nvvk::ValidationSettings validationSettings;
  validationSettings.setPreset(nvvk::ValidationSettings::LayerPresets::eStandard);
  vkSetup.instanceCreateInfoExt = validationSettings.buildPNextChain();

#if defined(USE_NSIGHT_AFTERMATH)
  // Adding the Aftermath extension to the device and initialize the Aftermath
  auto& aftermath = AftermathCrashTracker::getInstance();
  aftermath.initialize();
  aftermath.addExtensions(vkSetup.deviceExtensions);
  // The callback function is called when a validation error is triggered. This will wait to give
  // time to dump the GPU crash.
  nvvk::CheckError::getInstance().setCallbackFunction([&](VkResult result)
                                                      { aftermath.errorCallback(result); });
#endif
  return vkSetup;
}

template nvvk::ContextInitInfo
createVkContextInfo<core::ApplicationCreateInfo>(const core::ApplicationCreateInfo& appInfo);
template nvvk::ContextInitInfo
createVkContextInfo<nvapp::ApplicationCreateInfo>(const nvapp::ApplicationCreateInfo& appInfo);

}  // namespace vk_utils
