#pragma once

#include <shaders/shaderio.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <nvvk/gbuffers.hpp>

#include "backend/interfaces/IRenderGraph.hpp"
#include "backend/interfaces/IToneMapper.hpp"
#include "backend/vulkan/core/ContextManager.hpp"

// virtual void init(class VulkanContextManager* core) = 0;
// virtual void execute(const IRenderContext& ctx) = 0;
// virtual void deinit() = 0;
class VulkanToneMapper : public IToneMapper, public IRenderPass
{
public:
  VulkanToneMapper();
  ~VulkanToneMapper() override;

  void init(VulkanContextManager* core,
            const SceneResourcesManager& scene) override;
  void deinit(VulkanContextManager* core) override;
  void execute(const IRenderContext& ctx) override;

  // Explicitly non-copyable
  VulkanToneMapper(const VulkanToneMapper&) = delete;
  VulkanToneMapper& operator=(const VulkanToneMapper&) = delete;

private:
  nvshaders::Tonemapper m_tonemapper{};
  bool m_initialized = false;
};
