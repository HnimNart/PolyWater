#pragma once

#include <shaders/shaderio.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <nvvk/gbuffers.hpp>

#include "backend/interfaces/IRenderGraph.hpp"
#include "backend/interfaces/IToneMapper.hpp"
#include "backend/vulkan/core/ContextManager.hpp"

class ToneMapPass : public IToneMapper, public IRenderPass
{
public:
  ToneMapPass();
  ~ToneMapPass() override;

  void init(VulkanContextManager* core,
            const SceneResourcesManager& scene) override;

  void setup(PassBuilder& builder) override;
  void deinit(VulkanContextManager* core) override;
  void execute(const IRenderContext& ctx) override;

  // Explicitly non-copyable
  ToneMapPass(const ToneMapPass&) = delete;
  ToneMapPass& operator=(const ToneMapPass&) = delete;

private:
  nvshaders::Tonemapper m_tonemapper{};
  bool m_initialized = false;
};
