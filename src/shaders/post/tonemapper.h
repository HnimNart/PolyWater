#pragma once

#include <shaders/shaderio.h>
#include <vulkan/vulkan.h>

#include <nvshaders_host/sky.hpp>
#include <nvshaders_host/tonemapper.hpp>
#include <nvvk/gbuffers.hpp>
#include <scene/scene_context.hpp>

class PostProcessor
{
public:
  void init(VulkanContext* ctx);
  void run(VkCommandBuffer cmd, const nvvk::GBuffer& gbuffer);
  void clear();

  shaderio::TonemapperData& data();

private:
  nvshaders::SkySimple m_sky{};
  nvshaders::Tonemapper m_tonemapper{};
  shaderio::TonemapperData m_tonemapperData{};
};