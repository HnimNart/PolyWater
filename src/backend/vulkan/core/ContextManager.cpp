#include "ContextManager.hpp"

#include <vulkan/vk_enum_string_helper.h>

#include <nvvk/check_error.hpp>
#include <nvvk/commands.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/helpers.hpp>
#include <nvvk/validation_settings.hpp>

bool VulkanContextManager::init(const core::ApplicationCreateInfo& appInfo)
{
  // 1. Define Feature Structs
  VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjectFeatures{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT};

  VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};

  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};

  // 2. Setup Validation Settings
  nvvk::ValidationSettings validationSettings;
  validationSettings.setPreset(nvvk::ValidationSettings::LayerPresets::eStandard);

  // 3. Configure Context
  nvvk::ContextInitInfo vkSetup;
  vkSetup.instanceCreateInfoExt = validationSettings.buildPNextChain();
  vkSetup.instanceExtensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};

  vkSetup.deviceExtensions = {
      {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME},
      {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjectFeatures},
      {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &accelFeature},
      {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rtPipelineFeature},
      {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME},
  };

  if (!appInfo.headless)
  {
    nvvk::addSurfaceExtensions(vkSetup.instanceExtensions, &vkSetup.deviceExtensions);
  }

  // 4. Initialize Vulkan Context
  VkResult result = m_vkContext.init(vkSetup);
  if (result != VK_SUCCESS)
  {
    LOGE("Vulkan Initialization Failed: %s\n", string_VkResult(result));
    return false;
  }

  // 5. Setup pools and allocators
  setupTransientCommandPool();
  setupDescriptorPool();
  setupAllocator();

  return true;
}

void VulkanContextManager::setupTransientCommandPool()
{
  VkDevice device = m_vkContext.getDevice();
  const nvvk::QueueInfo& graphicsQueue = m_vkContext.getQueueInfo(0);

  const VkCommandPoolCreateInfo cmdPoolInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
      .queueFamilyIndex = graphicsQueue.familyIndex,
  };
  NVVK_CHECK(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &m_transientCmdPool));
  NVVK_DBG_NAME(m_transientCmdPool);
}

void VulkanContextManager::setupDescriptorPool()
{
  VkDevice device = m_vkContext.getDevice();

  const std::array<VkDescriptorPoolSize, 1> poolSizes{{
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_maxTexturePool},
  }};

  const VkDescriptorPoolCreateInfo poolInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT |
               VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
      .maxSets = m_maxTexturePool,
      .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
      .pPoolSizes = poolSizes.data(),
  };
  NVVK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool));
  NVVK_DBG_NAME(m_descriptorPool);
}

void VulkanContextManager::setupAllocator()
{
  VmaAllocatorCreateInfo allocatorInfo = {
      .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
      .physicalDevice = m_vkContext.getPhysicalDevice(),
      .device = m_vkContext.getDevice(),
      .instance = m_vkContext.getInstance(),
      .vulkanApiVersion = VK_API_VERSION_1_4,
  };

  m_allocator.init(allocatorInfo);
  m_stagingUploader.init(&m_allocator);
}

VkCommandBuffer VulkanContextManager::startSingleTimeCmd()
{
  VkCommandBuffer cmd;
  NVVK_CHECK(nvvk::beginSingleTimeCommands(cmd, getDevice(), m_transientCmdPool));
  return cmd;
}

void VulkanContextManager::endSingleTimeCmd(VkCommandBuffer cmd)
{
  NVVK_CHECK(nvvk::endSingleTimeCommands(cmd, getDevice(), m_transientCmdPool,
                                         m_vkContext.getQueueInfo(0).queue));
}

void VulkanContextManager::waitForDeviceIdle()
{
  VkDevice device = getDevice();
  assert(device != VK_NULL_HANDLE);
  NVVK_CHECK(vkDeviceWaitIdle(device));
}

void VulkanContextManager::deinit()
{
  VkDevice device = m_vkContext.getDevice();
  assert(device != VK_NULL_HANDLE);
  NVVK_CHECK(vkDeviceWaitIdle(device));

  m_stagingUploader.deinit();
  m_allocator.deinit();

  vkDestroyCommandPool(device, m_transientCmdPool, nullptr);
  vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);

  m_vkContext.deinit();
}
