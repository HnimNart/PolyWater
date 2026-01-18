#pragma once

#include <shaders/shaderio.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <nvvk/gbuffers.hpp>

#include "backend/interfaces/IToneMapper.hpp"
#include "backend/vulkan/core/Backend.hpp"

class VulkanToneMapper : public IToneMapper
{
public:
  explicit VulkanToneMapper(VulkanBackend* backend);
  ~VulkanToneMapper() override;

  void init();
  void deinit();
  void run(VkCommandBuffer cmd, nvvk::GBuffer& gBuffers);

  // Explicitly non-copyable
  VulkanToneMapper(const VulkanToneMapper&) = delete;
  VulkanToneMapper& operator=(const VulkanToneMapper&) = delete;

private:
  VulkanBackend* m_backend = nullptr;
  nvshaders::Tonemapper m_tonemapper{};
  bool m_initialized = false;
};
