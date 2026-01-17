#pragma once

#include <shaders/shaderio.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <nvvk/gbuffers.hpp>

#include "_autogen/tonemapper.slang.h"
#include "backend/vulkan/VulkanBackend.hpp"
#include "shaders/post/IToneMapper.hpp"

class VulkanPostProcessor : public IPostProcessor
{
public:
  VulkanPostProcessor(core::VulkanBackend* backend)
  {
    m_tonemapper.init(&backend->allocator(), std::span(tonemapper_slang));
  }

  ~VulkanPostProcessor() override { deinit(); }

  void run(VkCommandBuffer cmd, nvvk::GBuffer& mGBuffers)
  {
    m_tonemapper.runCompute(cmd, mGBuffers.getSize(), m_tonemapperData,
                            mGBuffers.getDescriptorImageInfo(0),
                            mGBuffers.getDescriptorImageInfo(1));
  }

private:
  void deinit() { m_tonemapper.deinit(); }
  nvshaders::Tonemapper m_tonemapper{};
};
