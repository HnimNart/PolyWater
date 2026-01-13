#pragma once

#include <shaders/shaderio.h>
#include <vulkan/vulkan.h>

#include <nvshaders_host/sky.hpp>
#include <nvshaders_host/tonemapper.hpp>
#include <nvvk/gbuffers.hpp>

#include "_autogen/tonemapper.slang.h"
#include "backend/vulkan/VulkanBackend.hpp"

class PostProcessor
{
public:
  void init(core::VulkanBackend* backend)
  {
    // Initialize the tonemapper also with proe-compiled shader
    m_tonemapper.init(&backend->allocator(), std::span(tonemapper_slang));
  }
  void run(VkCommandBuffer cmd, const nvvk::GBuffer& gBuffers)
  {
    // Default post-processing: tonemapping
    m_tonemapper.runCompute(cmd, gBuffers.getSize(), m_tonemapperData,
                            gBuffers.getDescriptorImageInfo(0), gBuffers.getDescriptorImageInfo(1));
  }
  void deinit() { m_tonemapper.deinit(); }

  shaderio::TonemapperData& data() { return m_tonemapperData; }

private:
  nvshaders::Tonemapper m_tonemapper{};
  shaderio::TonemapperData m_tonemapperData{};
};
