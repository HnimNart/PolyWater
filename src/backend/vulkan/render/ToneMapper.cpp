#include "ToneMapper.hpp"

#include "_autogen/tonemapper.slang.h"

VulkanToneMapper::VulkanToneMapper(VulkanContextManager* backend) : m_backend(backend)
{
}

VulkanToneMapper::~VulkanToneMapper()
{
  deinit();
}

void VulkanToneMapper::init()
{
  if (m_initialized)
  {
    return;
  }

  // Initialize the tonemapper using the shader bytecode from the autogen header
  m_tonemapper.init(
      &m_backend->getAllocator(),
      std::span<const uint32_t>(tonemapper_slang, sizeof(tonemapper_slang) / sizeof(uint32_t)));

  m_initialized = true;
}

void VulkanToneMapper::deinit()
{
  m_tonemapper.deinit();
  m_initialized = false;
}

void VulkanToneMapper::run(VkCommandBuffer cmd, nvvk::GBuffer& gBuffers)
{
  if (!m_initialized)
  {
    return;
  }

  VkDescriptorImageInfo inputColor = gBuffers.getDescriptorImageInfo(0);
  VkDescriptorImageInfo outputColor = gBuffers.getDescriptorImageInfo(1);
  m_tonemapper.runCompute(cmd, gBuffers.getSize(), m_tonemapperData, inputColor, outputColor);
}
