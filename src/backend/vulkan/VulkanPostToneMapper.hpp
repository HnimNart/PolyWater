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
  VulkanPostProcessor() = default;
  void init(core::VulkanBackend* backend)
  {
    // Initialize the tonemapper also with proe-compiled shader
    m_tonemapper.init(&backend->allocator(), std::span(tonemapper_slang));
  }
  void run(VkCommandBuffer cmd, nvvk::GBuffer& m_gBuffers)
  {
    // Default post-processing: tonemapping
    m_tonemapper.runCompute(cmd, m_gBuffers.getSize(), m_tonemapperData,
                            m_gBuffers.getDescriptorImageInfo(0),
                            m_gBuffers.getDescriptorImageInfo(1));
  }
  void deinit() { m_tonemapper.deinit(); }

private:
  nvshaders::Tonemapper m_tonemapper{};
};
