#pragma once

#include <volk.h>

#include "backend/interfaces/rhi_definitions.hpp"

namespace vkb
{

// Helper to convert Enums
struct VulkanStateInfo
{
  VkImageLayout layout;
  VkAccessFlags2 access;
};

/**********************************************************/
inline VulkanStateInfo toVulkan(ResourceState state)
/**********************************************************/
{
  switch (state)
  {
    case ResourceState::Undefined:
      return {VK_IMAGE_LAYOUT_UNDEFINED, 0};

    case ResourceState::General:
      return {VK_IMAGE_LAYOUT_GENERAL,
              VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT};

    case ResourceState::RenderTarget:
      return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};

    case ResourceState::DepthRead:
      return {VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT};

    case ResourceState::DepthWrite:
      return {VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};

    case ResourceState::ShaderResource:
      return {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              VK_ACCESS_2_SHADER_READ_BIT};

    case ResourceState::TransferSrc:
      return {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_ACCESS_2_TRANSFER_READ_BIT};

    case ResourceState::TransferDst:
      return {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_ACCESS_2_TRANSFER_WRITE_BIT};

    case ResourceState::Present:
      return {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
              VK_ACCESS_2_NONE};  // Presentation engine handles final read
  }
  return {VK_IMAGE_LAYOUT_UNDEFINED, 0};
}

/**********************************************************/
inline VkPipelineStageFlags2 toVulkan(PipelineStage stage)
/**********************************************************/
{
  switch (stage)
  {
    case PipelineStage::TopOfPipe:
      return VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;

    case PipelineStage::Vertex:
      return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;

    case PipelineStage::Fragment:
      return VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

    case PipelineStage::Compute:
      return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

    case PipelineStage::RayTracing:
      return VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;

    case PipelineStage::RenderTarget:
      // Covers Color, Depth, and Stencil output stages
      return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
             VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
             VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

    case PipelineStage::Transfer:
      return VK_PIPELINE_STAGE_2_TRANSFER_BIT;

    case PipelineStage::AllGraphics:
      return VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

    case PipelineStage::BottomOfPipe:
      return VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
  }
  return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
}

}  // namespace vkb
