#pragma once

#include <shaders/shaderio.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <nvvk/gbuffers.hpp>

#include "backend/vulkan/VulkanBackend.hpp"
#include "shaders/post/IToneMapper.hpp"

class VulkanPostProcessor : public IPostProcessor
{
public:
  explicit VulkanPostProcessor(core::VulkanBackend* backend);
  ~VulkanPostProcessor() override;

  void init();
  void deinit();
  void run(VkCommandBuffer cmd, nvvk::GBuffer& gBuffers);

  // Explicitly non-copyable
  VulkanPostProcessor(const VulkanPostProcessor&) = delete;
  VulkanPostProcessor& operator=(const VulkanPostProcessor&) = delete;

private:
  core::VulkanBackend* m_backend = nullptr;
  nvshaders::Tonemapper m_tonemapper{};
  bool m_initialized = false;
};
