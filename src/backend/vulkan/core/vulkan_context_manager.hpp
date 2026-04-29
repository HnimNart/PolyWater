#pragma once

#include <volk.h>

#include <nvvk/check_error.hpp>
#include <nvvk/commands.hpp>
#include <nvvk/context.hpp>
#include <nvvk/resource_allocator.hpp>
#include <nvvk/staging.hpp>
#include <nvvk/swapchain.hpp>
#include <nvvk/validation_settings.hpp>

#include "app/app_info.hpp"
#include "shaders/shared/structs.h"

class VulkanContextManager
{
public:
  bool init(const app::ApplicationCreateInfo& appInfo);
  void deinit();

  // Accessors
  VkDevice getDevice() const
  {
    return m_vkContext.getDevice();
  }
  VkPhysicalDevice getPhysicalDevice() const
  {
    return m_vkContext.getPhysicalDevice();
  }
  VkInstance getInstance() const
  {
    return m_vkContext.getInstance();
  }
  const nvvk::QueueInfo& getQueueInfo(uint32_t index) const
  {
    return m_vkContext.getQueueInfo(index);
  }
  VkDescriptorPool getDescriptorPool() const
  {
    return m_descriptorPool;
  }
  VkCommandPool getTransientCmdPool() const
  {
    return m_transientCmdPool;
  }

  nvvk::ResourceAllocator& getAllocator()
  {
    return m_allocator;
  }
  nvvk::StagingUploader& getStagingUploader()
  {
    return m_stagingUploader;
  }

  // Utility methods
  VkCommandBuffer startSingleTimeCmd();
  void endSingleTimeCmd(VkCommandBuffer cmd);
  void waitForDeviceIdle();

private:
  void setupAllocator();
  nvvk::Context m_vkContext;
  VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
  VkCommandPool m_transientCmdPool = VK_NULL_HANDLE;

  nvvk::ResourceAllocator m_allocator;
  nvvk::StagingUploader m_stagingUploader;

  static constexpr uint32_t m_maxTexturePool = MAX_SCENE_TEXTURES;

  void setupDescriptorPool();
  void setupTransientCommandPool();
};
