#pragma once
#include <nvshaders_host/sky.hpp>
#include <nvutils/camera_manipulator.hpp>
#include <nvvk/gbuffers.hpp>

#include "backend/interfaces/IRenderGraph.hpp"
#include "backend/vulkan/core/ContextManager.hpp"

// Generated Shaders
#include "_autogen/sky_simple.slang.h"

// -----------------------------------------------------------------------------
// Sky Pass: Handles the procedural skybox via Compute Shader
// -----------------------------------------------------------------------------
class SkyPass : public IRenderPass {
public:
  void init(VulkanContextManager *core) override;
  void setup(PassBuilder &builder) override;
  void deinit(VulkanContextManager *core) override;
  void execute(const IRenderContext &ctx) override;

private:
  VulkanContextManager *m_core = nullptr;
  nvshaders::SkySimple m_skySimple;
};
