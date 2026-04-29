#include "vulkan_context_manager.hpp"

#include <vulkan/vk_enum_string_helper.h>

#include <nvvk/check_error.hpp>
#include <nvvk/commands.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/helpers.hpp>
#include <nvvk/validation_settings.hpp>

/**********************************************************/
bool VulkanContextManager::init(const app::ApplicationCreateInfo& appInfo)
/**********************************************************/
{
  // Do not set the pNext pointers manually. Let nvvk handle the chaining.
  VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
  meshFeatures.meshShader = VK_TRUE;
  meshFeatures.taskShader = VK_TRUE;
  meshFeatures.primitiveFragmentShadingRateMeshShader = VK_FALSE;

  VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjFeatures{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT};
  shaderObjFeatures.shaderObject = VK_TRUE;

  VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dynamicState3Features{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT};
  dynamicState3Features.extendedDynamicState3PolygonMode = VK_TRUE;

  VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  accelFeature.accelerationStructure = VK_TRUE;

  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
  rtPipelineFeature.rayTracingPipeline = VK_TRUE;

  VkPhysicalDeviceFragmentShadingRateFeaturesKHR fsrFeatures{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};

  // Setup Context Initialization
  nvvk::ContextInitInfo vkSetup;
  vkSetup.apiVersion = VK_API_VERSION_1_4;
  vkSetup.enableAllFeatures = false;

  // Attach each feature to its corresponding extension
  vkSetup.deviceExtensions = {
      // Allows pushing descriptor updates directly into a command buffer
      // instead of writing to descriptor set objects
      {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME, nullptr, true},

      // Enables hardware-accelerated ray tracing pipelines (RayGen, Miss,
      // ClosestHit, etc.)
      {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rtPipelineFeature, true},

      // Allows using independent, dynamic shader objects instead of monolithic
      // Pipeline State Objects (PSOs)
      {VK_EXT_SHADER_OBJECT_EXTENSION_NAME, &shaderObjFeatures, true},

      // Enables the highly parallel compute-like mesh shading pipeline to
      // replace the traditional vertex pipeline
      {VK_EXT_MESH_SHADER_EXTENSION_NAME, &meshFeatures, true},

      // Reduces PSO permutations by allowing states (like Polygon Mode) to be
      // bound dynamically at recording time
      {VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME, &dynamicState3Features,
       true},

      // Provides the ability to build and manage Bounding Volume Hierarchies
      // (BVHs) required for ray tracing
      {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &accelFeature, true},

      // Required by ray tracing extensions; allows the driver to offload heavy
      // operations (like BVH builds) to background threads
      {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, nullptr, true},

      // Enables Variable Rate Shading (VRS) to decouple fragment shading
      // execution rate from the render target resolution
      // {VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME, &fsrFeatures, true},
  };

  // Validation and Instance Setup
  nvvk::ValidationSettings validationSettings;
  validationSettings.setPreset(
      nvvk::ValidationSettings::LayerPresets::eStandard);
  vkSetup.instanceCreateInfoExt = validationSettings.buildPNextChain();
  vkSetup.instanceExtensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};

  if (!appInfo.headless)
  {
    nvvk::addSurfaceExtensions(vkSetup.instanceExtensions,
                               &vkSetup.deviceExtensions);
  }

  // Initialize
  VkResult result = m_vkContext.init(vkSetup);
  if (result != VK_SUCCESS)
  {
    LOGE("Vulkan Initialization Failed: %s\n", string_VkResult(result));
    return false;
  }

  // Setup pools and allocators
  setupTransientCommandPool();
  setupDescriptorPool();
  setupAllocator();

  return true;
}

/**********************************************************/
void VulkanContextManager::setupTransientCommandPool()
/**********************************************************/
{
  VkDevice device = m_vkContext.getDevice();
  const nvvk::QueueInfo& graphicsQueue = m_vkContext.getQueueInfo(0);

  const VkCommandPoolCreateInfo cmdPoolInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
      .queueFamilyIndex = graphicsQueue.familyIndex,
  };
  NVVK_CHECK(
      vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &m_transientCmdPool));
  NVVK_DBG_NAME(m_transientCmdPool);
}

/**********************************************************/
void VulkanContextManager::setupDescriptorPool()
/**********************************************************/
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
  NVVK_CHECK(
      vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool));
  NVVK_DBG_NAME(m_descriptorPool);
}

/**********************************************************/
void VulkanContextManager::setupAllocator()
/**********************************************************/
{
  VmaAllocatorCreateInfo allocatorInfo = {
      .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
      .physicalDevice = m_vkContext.getPhysicalDevice(),
      .device = m_vkContext.getDevice(),
      .instance = m_vkContext.getInstance(),
      .vulkanApiVersion = VK_API_VERSION_1_4,
  };

  m_allocator.init(allocatorInfo);
  m_stagingUploader.init(&m_allocator, true);
}

/**********************************************************/
VkCommandBuffer VulkanContextManager::startSingleTimeCmd()
/**********************************************************/
{
  VkCommandBuffer cmd;
  NVVK_CHECK(
      nvvk::beginSingleTimeCommands(cmd, getDevice(), m_transientCmdPool));
  return cmd;
}

/**********************************************************/
void VulkanContextManager::endSingleTimeCmd(VkCommandBuffer cmd)
/**********************************************************/
{
  NVVK_CHECK(nvvk::endSingleTimeCommands(cmd, getDevice(), m_transientCmdPool,
                                         m_vkContext.getQueueInfo(0).queue));
}

/**********************************************************/
void VulkanContextManager::waitForDeviceIdle()
/**********************************************************/
{
  VkDevice device = getDevice();
  assert(device != VK_NULL_HANDLE);
  NVVK_CHECK(vkDeviceWaitIdle(device));
}

/**********************************************************/
void VulkanContextManager::deinit()
/**********************************************************/
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
