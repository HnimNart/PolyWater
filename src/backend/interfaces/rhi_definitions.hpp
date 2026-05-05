#pragma once

#include <cstdint>

// Generic Resource States
enum class ResourceState
{
  Undefined,
  General,         // Read/Write (e.g., Storage Image)
  RenderTarget,    // Color Attachment
  DepthRead,       // Depth Test (Read-Only)
  DepthWrite,      // Depth Write
  ShaderResource,  // Texture Sampled (Read-Only)
  TransferSrc,     // Copy Source
  TransferDst,     // Copy Dest
  Present          // Ready for Swapchain
};

// Generic Pipeline Stages
enum class PipelineStage
{
  // clang-format off
  TopOfPipe,    // The very start of the GPU command processor, before any work begins
  Vertex,       // Processing vertex shaders, input assembly, and geometry data
  Fragment,     // The pixel/fragment shader stage where lighting and texturing occur
  Compute,      // General-purpose execution for compute shaders (GPGPU)
  RayTracing,   // Acceleration structure traversal and ray shader execution (RTX/KHR)
  RenderTarget, // The hardware stage that writes final colors and depth/stencil values
  Transfer,     // Dedicated hardware for copy, blit, and memory clear operations
  AllGraphics,  // A synchronization shorthand covering the entire 3D drawing pipeline
  BottomOfPipe  // The very end of the GPU pipeline after all work is fully retired
  // clang-format on
};

enum RenderOutput : uint8_t
{
  Linear = 0,       // HDR raw
  ToneMapped = 1,   // SDR presentation
  AccumLinear = 2,  // HDR accumulated
  Denoised = 3,     // HDR clean
  Albedo = 4,       // First-hit albedo (OIDN auxiliary input)
  Normal = 5,       // First-hit world-space normal (OIDN auxiliary input)
  DepthBuffer = 6,  // Depth (handled separately by GBuffer)
  Swapchain = 7,    // Swapchain (handled by SwapchainManager)
  Count = 8,
};

// A Generic Barrier "Instruction" calculated by the Graph
struct BarrierInfo
{
  RenderOutput resource;
  ResourceState oldState;
  PipelineStage srcStage;
  ResourceState newState;
  PipelineStage dstStage;
};

// Identifies which per-frame command buffer a pass records into.
// One primary command buffer is allocated per slot every frame, reused across
// frames by resetting the per-frame pool rather than reallocating.
//
// Slot layout:
//   Main    – ray trace / raster / sky / meshlet / mip-reduction
//   Denoise – OIDN or compute denoiser
//   ToneMap – tonemapping compute pass
//   Gui     – Dear ImGui overlay (recorded into the swapchain attachment)
//   Count   – sentinel / "end current pass" signal (not a real slot)
enum class PassCmdSlot : uint32_t
{
  Main    = 0,
  Denoise = 1,
  ToneMap = 2,
  Gui     = 3,
  Count   = 4,  // Total number of real slots; also used as "no slot" sentinel
};

// This maps to enum VkIndexType
enum IndexType
{
  IndexType16 = 0,
  IndexType32 = 1,
  IndexType8 = 1000265000,
};
