#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <vector>

#ifdef None
#undef None
#endif
#include <OpenImageDenoise/oidn.hpp>
#include <nvvk/gbuffers.hpp>

#include "backend/vulkan/core/vulkan_context_manager.hpp"
#include "backend/vulkan/core/vulkan_frame_synchronization_manager.hpp"
#include "renderer/interfaces/render_graph_interface.hpp"

// ---------------------------------------------------------------------------
// OIDNDenoisePass
//
// GPU-side AI denoiser using Intel Open Image Denoise (OIDN) v2.4+.
// Requires CUDA — no CPU fallback.
//
// Asynchronous N-buffered architecture (three-part split command buffer)
// -----------------------------------------------------------------------
//  1. Pre-Denoise (Vulkan):  The current command buffer records copies of the
//     Linear, Albedo, and Normal G-buffers into N-buffered OIDN-mapped
//     external VkBuffers.  The buffer is ended and registered in
//     VulkanRenderContext::preCommandBuffers; endFrame() submits it,
//     signalling Timeline Semaphore A, before the main submit.
//
//  2. Denoise (CUDA):  On a dedicated CUDA stream the pass enqueues:
//       - cudaWaitExternalSemaphoresAsync (wait for Semaphore A)
//       - filter.executeAsync()           (OIDN "RT" beauty filter)
//       - cudaSignalExternalSemaphoresAsync (signal Semaphore B)
//     All three operations are non-blocking on the CPU.
//
//  3. Post-Denoise (Vulkan):  A per-frame, pre-allocated command buffer is
//     reset, begun, and used to record a memory barrier (for CUDA writes) and
//     the copy from the OIDN output buffer to the Denoised G-buffer image.
//     The buffer is stored in IRenderContext::cmdBuffer so subsequent passes
//     (ToneMap, UI) keep recording into it.  The frame-sync manager is told
//     to wait on Semaphore B before its final submit.
//
// N-buffering
// -----------
//  All per-frame OIDN resources — four ExternalBuffers and an oidn::FilterRef —
//  are replicated once per frame-in-flight slot (m_numFrames, typically 3).
//  The slot is derived from
//  VulkanFrameSynchronizationManager::getCurrentFrameIndex().
//
// Timeline counters
// -----------------
//  Each frame-ring slot owns an independent pair of timeline semaphores so
//  that each slot's CUDA stream can signal its own semaphore in any order
//  relative to other slots.  The semaphore value used per slot is
//  vkCtx.frameNumber, which advances by m_numFrames each cycle and is
//  therefore strictly monotone within each slot's semaphore pair.
// ---------------------------------------------------------------------------
class OIDNDenoisePass final : public IRenderPass
{
public:
  explicit OIDNDenoisePass(VulkanContextManager* contextManager,
                           VulkanFrameSynchronizationManager* frameSyncManager);

  void init() override;
  void deinit() override;
  void setup(PassBuilder& builder) override;
  void execute(IRenderContext& ctx) override;

private:
  // -------------------------------------------------------------------------
  // Internal types
  // -------------------------------------------------------------------------
  struct ExternalBuffer
  {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    size_t byteSize = 0;
    oidn::BufferRef oidnBuf;
  };

  // Per-frame (N-buffered) resources.  One set per frame-ring slot.
  struct FrameResources
  {
    ExternalBuffer colorBuf;
    ExternalBuffer albedoBuf;
    ExternalBuffer normalBuf;
    ExternalBuffer outputBuf;
    oidn::DeviceRef oidnDevice;  // Dedicated device, permanently bound to this slot's stream
    oidn::FilterRef filter;
    VkCommandBuffer postCmdBuf = VK_NULL_HANDLE;  // Owned by m_postCmdPool
  };

  // -------------------------------------------------------------------------
  // Helpers
  // -------------------------------------------------------------------------
  void createCudaStreams();
  void createOIDNDevices();
  void createSemaphores();
  void destroySemaphores();
  void createCudaResources();
  void destroyCudaResources();

  void createFrameResources(uint32_t width, uint32_t height);
  void destroyFrameResources();
  void rebuildFilters(uint32_t width, uint32_t height);

  // GPU path: exportable device-local memory shared with OIDN via handle.
  ExternalBuffer allocateExternalBuffer(size_t byteSize, VkBufferUsageFlags usage,
                                        oidn::DeviceRef& oidnDevice);
  void destroyBuffer(ExternalBuffer& buf);

  void copyBufferToDenoised(VkCommandBuffer cmd, const nvvk::GBuffer* gBuffers,
                            VkExtent2D size, uint32_t frameIdx);

  // -------------------------------------------------------------------------
  // Members
  // -------------------------------------------------------------------------
  VulkanContextManager* m_contextManager = nullptr;
  VulkanFrameSynchronizationManager* m_frameSyncManager = nullptr;

  // N-buffered per-frame resources and their shared command pool.
  std::vector<FrameResources> m_frameResources;
  uint32_t m_numFrames = 0;
  VkCommandPool m_postCmdPool = VK_NULL_HANDLE;

  // Per-slot Vulkan timeline semaphore pairs for Vulkan ↔ CUDA synchronisation.
  //
  // Each frame-ring slot owns an independent pair so that CUDA streams for
  // different slots can signal their semaphores in any order without violating
  // the strictly-increasing requirement that a single shared timeline semaphore
  // would impose.  Using one semaphore per slot also removes the inter-slot
  // ordering dependency that would serialise OIDN execution across slots on
  // a single CUDA stream.
  //
  //  m_semVulkanToCuda[i]  — signalled exclusively by Vulkan for slot i,
  //                          waited by that slot's CUDA stream.
  //  m_semCudaToVulkan[i]  — signalled exclusively by that slot's CUDA stream
  //                          (or the CPU fallback), waited by Vulkan final submit.
  //
  // The semaphore value used each time a slot is reused is vkCtx.frameNumber,
  // which increases by m_numFrames every cycle, keeping it monotone per slot.
  std::vector<VkSemaphore> m_semVulkanToCuda;
  std::vector<VkSemaphore> m_semCudaToVulkan;

  // CUDA interop handles (GPU path only), one entry per frame-ring slot.
  // Stored as void* to avoid pulling <cuda_runtime_api.h> into this header.
  std::vector<void*> m_cudaStreams;              // cudaStream_t, one per slot
  std::vector<void*> m_cudaExtSemVulkanToCuda;  // cudaExternalSemaphore_t, one per slot
  std::vector<void*> m_cudaExtSemCudaToVulkan;  // cudaExternalSemaphore_t, one per slot

  uint32_t m_width = 0;
  uint32_t m_height = 0;
};
