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

struct VulkanContext
{
  nvvk::Context context;
  VkDescriptorPool descriptorPool{};
  nvvk::ResourceAllocator allocator;
  nvvk::StagingUploader stagingUploader;
  const VkExtent2D& viewportSize;
  std::shared_ptr<SlangShaderCompiler> slangCompiler;

  VulkanContext(const nvvk::Context& _context, VkDescriptorPool _descriptorPool,
                const VkExtent2D& _viewportSize, std::shared_ptr<SlangShaderCompiler> _compiler) :
      context(_context), descriptorPool(_descriptorPool), viewportSize(_viewportSize),
      slangCompiler(_compiler)
  {
  }
};

static std::shared_ptr<VulkanContext>
create_vk_context(nvvk::Context& vkContext, VkDescriptorPool descriptorPool,
                  const VkExtent2D& viewportSize,
                  std::shared_ptr<SlangShaderCompiler> compiler = nullptr)
{
  auto vkCtx = std::make_shared<VulkanContext>(vkContext, descriptorPool, viewportSize, compiler);

  VmaAllocatorCreateInfo allocatorInfo = {
      .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
      .physicalDevice = vkContext.getPhysicalDevice(),
      .device = vkContext.getDevice(),
      .instance = vkContext.getInstance(),
      .vulkanApiVersion = VK_API_VERSION_1_4,
  };

  vkCtx->allocator.init(allocatorInfo);
  vkCtx->stagingUploader.init(&vkCtx->allocator, true);

  return vkCtx;
}
