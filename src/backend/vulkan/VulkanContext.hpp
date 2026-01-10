#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <nvvk/acceleration_structures.hpp>  // Acceleration structure management
#include <nvvk/context.hpp>
#include <nvvk/descriptors.hpp>
#include <nvvk/gbuffers.hpp>  // GBuffer management
#include <nvvk/graphics_pipeline.hpp>
#include <nvvk/sampler_pool.hpp>
#include <nvvk/sbt_generator.hpp>

#include "shaders/compiler/slang.hpp"

class VulkanContext
{
public:
  // We store the context by reference or copy depending on ownership.
  // Here we assume it's a reference to the long-lived app context.
  const nvvk::Context& context;
  VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
  nvvk::ResourceAllocator allocator;
  nvvk::StagingUploader stagingUploader;
  const VkExtent2D& viewportSize;
  std::shared_ptr<SlangShaderCompiler> slangCompiler;

  static std::shared_ptr<VulkanContext>
  create(const nvvk::Context& vkContext, VkDescriptorPool descriptorPool,
         const VkExtent2D& viewportSize, std::shared_ptr<SlangShaderCompiler> compiler = nullptr)
  {
    return std::make_shared<VulkanContext>(vkContext, descriptorPool, viewportSize, compiler);
  }

  VulkanContext(const nvvk::Context& _context, VkDescriptorPool _descriptorPool,
                const VkExtent2D& _viewportSize, std::shared_ptr<SlangShaderCompiler> _compiler) :
      context(_context), descriptorPool(_descriptorPool), viewportSize(_viewportSize),
      slangCompiler(_compiler)
  {
    // Initialization logic moved inside the constructor for true RAII
    VmaAllocatorCreateInfo allocatorInfo = {
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = context.getPhysicalDevice(),
        .device = context.getDevice(),
        .instance = context.getInstance(),
        .vulkanApiVersion = VK_API_VERSION_1_4,
    };

    allocator.init(allocatorInfo);
    allocator.setLeakID(81);

    // Initialize staging uploader with the allocator
    stagingUploader.init(&allocator);
  }

  // The Destructor: This is what solves your "Object not destroyed" errors
  ~VulkanContext() = default;

  // Disable copying to prevent double-destruction of Vulkan handles
  VulkanContext(const VulkanContext&) = delete;
  VulkanContext& operator=(const VulkanContext&) = delete;

  void deinit()
  {
    VkDevice device = context.getDevice();
    if (device == VK_NULL_HANDLE)
      return;

    // Wait for GPU to finish work before destroying resources
    vkDeviceWaitIdle(device);

    // 1. Deinit uploader first (it may use the allocator)
    stagingUploader.deinit();

    // 2. Deinit allocator (this destroys the leaked VkBuffer 0x1e...)
    allocator.deinit();
  }
};
