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
//
// Asynchronous N-buffered architecture (three-part split command buffer)
// -----------------------------------------------------------------------
//  1. Pre-Denoise (Vulkan):  The current command buffer records copies of the
//     Linear, Albedo, and Normal G-buffers into N-buffered OIDN-mapped
//     external VkBuffers.  The buffer is ended and submitted directly to the
//     graphics queue, signalling Timeline Semaphore A.
//
//  2. Denoise (CUDA):  On a dedicated CUDA stream the pass enqueues:
//       - cudaWaitExternalSemaphoresAsync (wait for Semaphore A)
//       - filter.executeAsync()           (OIDN "RT" beauty filter)
//       - cudaSignalExternalSemaphoresAsync (signal Semaphore B)
//     All three operations are non-blocking on the CPU.
//     CPU-only fallback: vkWaitSemaphores + filter.execute() +
//     vkSignalSemaphore.
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
// Timeline counter
// ----------------
//  m_timelineCounter is an independent, strictly monotonic uint64_t.  It is
//  incremented by 2 each execute() so that (counter-1) and (counter) are the
//  Semaphore A and Semaphore B signal values respectively.  It is never reset
//  and is unaffected by the engine's own frame-number recycling.
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
    void* hostPtr = nullptr;  // Non-null on the CPU/host-visible path
  };

  // Per-frame (N-buffered) resources.  One set per frame-ring slot.
  struct FrameResources
  {
    ExternalBuffer colorBuf;
    ExternalBuffer albedoBuf;
    ExternalBuffer normalBuf;
    ExternalBuffer outputBuf;
    oidn::FilterRef filter;
    VkCommandBuffer postCmdBuf = VK_NULL_HANDLE;  // Owned by m_postCmdPool
  };

  // -------------------------------------------------------------------------
  // Helpers
  // -------------------------------------------------------------------------
  void createOIDNDevice();
  void createTimelineSemaphore();
  void destroyTimelineSemaphore();
  void createCudaResources();
  void destroyCudaResources();

  void createFrameResources(uint32_t width, uint32_t height);
  void destroyFrameResources();
  void rebuildFilters(uint32_t width, uint32_t height);

  // GPU path: exportable device-local memory shared with OIDN via handle.
  ExternalBuffer allocateExternalBuffer(size_t byteSize,
                                        VkBufferUsageFlags usage);
  // CPU fallback: HOST_VISIBLE | HOST_COHERENT memory wrapped by OIDN.
  ExternalBuffer allocateHostBuffer(size_t byteSize, VkBufferUsageFlags usage);
  void destroyBuffer(ExternalBuffer& buf);

  void copyBufferToDenoised(VkCommandBuffer cmd, const nvvk::GBuffer* gBuffers,
                            VkExtent2D size, uint32_t frameIdx);

  // -------------------------------------------------------------------------
  // Members
  // -------------------------------------------------------------------------
  VulkanContextManager* m_contextManager = nullptr;
  VulkanFrameSynchronizationManager* m_frameSyncManager = nullptr;

  // OIDN device — one shared instance, committed once after stream is set.
  oidn::DeviceRef m_oidnDevice;
  bool m_gpuPath = false;  // True ↔ external-memory GPU buffers used

  // N-buffered per-frame resources and their shared command pool.
  std::vector<FrameResources> m_frameResources;
  uint32_t m_numFrames = 0;
  VkCommandPool m_postCmdPool = VK_NULL_HANDLE;

  // Dedicated Vulkan timeline semaphore for Vulkan ↔ CUDA synchronisation.
  // Uses its own monotonic counter, independent of the engine's frame counter.
  VkSemaphore m_timelineSemaphore = VK_NULL_HANDLE;
  uint64_t m_timelineCounter = 0;

  // CUDA interop handles (GPU path only).
  // Stored as void* to avoid pulling <cuda_runtime_api.h> into this header.
  void* m_cudaStream = nullptr;        // cudaStream_t
  void* m_cudaExtSemaphore = nullptr;  // cudaExternalSemaphore_t

  uint32_t m_width = 0;
  uint32_t m_height = 0;
};
