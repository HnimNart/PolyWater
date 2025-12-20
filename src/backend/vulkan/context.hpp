#pragma once

#include <vulkan/vulkan.h>

#include <nvvk/acceleration_structures.hpp>  // Acceleration structure management
#include <nvvk/descriptors.hpp>
#include <nvvk/gbuffers.hpp>  // GBuffer management
#include <nvvk/graphics_pipeline.hpp>
#include <nvvk/sampler_pool.hpp>
#include <nvvk/sbt_generator.hpp>

struct VulkanContext
{
  nvvk::ResourceAllocator* allocator = nullptr;
  VkPhysicalDevice physicalDevice{};
  VkDevice device{};
  nvvk::QueueInfo graphicsQueue;
  const VkExtent2D& viewportSize{};
  VkDescriptorPool textureDescriptorPool{};
  nvvk::StagingUploader& stagingUploader;  // Utility to upload data to the GPU
};
