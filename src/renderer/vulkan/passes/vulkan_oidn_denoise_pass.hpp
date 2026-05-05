#pragma once

#include <vulkan/vulkan_core.h>

#ifdef None
#undef None
#endif
#include <OpenImageDenoise/oidn.hpp>
#include <nvvk/gbuffers.hpp>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "backend/vulkan/core/vulkan_context_manager.hpp"
#include "renderer/interfaces/render_graph_interface.hpp"

// ---------------------------------------------------------------------------
// OIDNDenoisePass
//
// GPU-side AI denoiser using Intel Open Image Denoise (OIDN) v2.4+.
//
// Execution model — true async, CPU never blocks
// -----------------------------------------------
//  One FrameData slot is allocated per Vulkan frame-ring index (lazily, on
//  first use).  Each slot owns:
//    colorBuf / albedoBuf / normalBuf / outputBuf  – OIDN I/O buffers
//    filter     – committed OIDN "RT" filter
//    fence      – VkFence signaled when CB1 (image→buffer GPU copy) is done
//    cb3        – reused VkCommandBuffer for post-OIDN work (ToneMap, UI)
//
//  Per frame (execute()):
//   1. Copy GBuffer images → slot[ringIdx] OIDN input buffers  (CB1).
//   2. End CB1, submit with slot[ringIdx].fence.  No CPU wait.
//   3. Push WorkerJob onto the job queue.  No CPU wait.
//   4. Store m_oidnTimelineSemaphore + next signal value in VulkanRenderContext.
//      VulkanBackend::endFrame() adds this as a GPU-side wait on CB3's submit.
//   5. Begin slot[ringIdx].cb3, record barrier + outputBuf→Denoised copy.
//      Swap ctx.cmdBuffer to cb3 so ToneMap and UI continue recording into it.
//
//  Worker thread (per job):
//   a. vkWaitForFences(job.fence)  ← waits for CB1 to complete on GPU
//   b. filter.executeAsync() + sync()  ← OIDN inference (CPU or GPU backend)
//   c. vkSignalSemaphore(m_oidnTimelineSemaphore, job.signalValue)  ← CPU→GPU
//
//  endFrame():
//   – Submits CB3 with waitSemaphore = {imageAvailable, oidnTimeline@N}.
//   – GPU holds CB3 until step (c) fires.  CPU is free immediately.
//
//  Frame-ring safety
//  -----------------
//  waitForFrameCompletion() for ring-slot R waits for the frame-timeline
//  semaphore value that was signaled when CB3_{R_prev} completed.  CB3_prev
//  could only complete after the OIDN timeline semaphore was signaled (step c),
//  so by the time slot R is reused all OIDN I/O buffers are safe to overwrite.
// ---------------------------------------------------------------------------
class OIDNDenoisePass final : public IRenderPass
{
public:
  explicit OIDNDenoisePass(VulkanContextManager* contextManager);

  void init() override;
  void deinit() override;
  void setup(PassBuilder& builder) override;
  void execute(IRenderContext& ctx) override;

private:
  // -----------------------------------------------------------------------
  // Internal types
  // -----------------------------------------------------------------------
  struct ExternalBuffer
  {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    size_t byteSize = 0;
    oidn::BufferRef oidnBuf;
    void* hostPtr = nullptr;  // Non-null on the CPU/host-visible fallback path
  };

  // One slot per frame-ring index — OIDN I/O buffers, filter, and fences.
  struct FrameData
  {
    ExternalBuffer colorBuf;
    ExternalBuffer albedoBuf;
    ExternalBuffer normalBuf;
    ExternalBuffer outputBuf;
    oidn::FilterRef filter;
    VkFence fence{VK_NULL_HANDLE};   // Signaled when CB1 finishes on GPU
    VkCommandBuffer cb3{VK_NULL_HANDLE};  // Reused post-OIDN command buffer
    VkCommandPool cb3Pool{VK_NULL_HANDLE};  // Pool cb3 was allocated from
  };

  // Per-job context passed to the worker thread.
  struct WorkerJob
  {
    VkFence fence;
    VkDevice device;
    oidn::FilterRef filter;
    oidn::DeviceRef oidnDevice;
    VkSemaphore semaphore;   // m_oidnTimelineSemaphore
    uint64_t signalValue;    // Timeline value to signal after OIDN completes
  };

  // -----------------------------------------------------------------------
  // Helpers
  // -----------------------------------------------------------------------
  void createOIDNDevice();

  // Lazily allocate and initialise slot 'idx' for the current resolution.
  void ensureSlot(uint32_t idx, uint32_t width, uint32_t height);
  void destroySlot(FrameData& slot);
  void destroyAllSlots();

  void rebuildFilter(uint32_t slot, uint32_t width, uint32_t height);

  ExternalBuffer allocateExternalBuffer(size_t byteSize,
                                        VkBufferUsageFlags usage);
  ExternalBuffer allocateHostBuffer(size_t byteSize, VkBufferUsageFlags usage);
  void destroyBuffer(ExternalBuffer& buf);

  void copyBufferToDenoised(VkCommandBuffer cmd, const nvvk::GBuffer* gBuffers,
                            VkExtent2D size, VkBuffer outputBuffer);

  void oidnWorkerLoop();

  // -----------------------------------------------------------------------
  // Members
  // -----------------------------------------------------------------------
  VulkanContextManager* m_contextManager = nullptr;
  oidn::DeviceRef m_oidnDevice;
  bool m_gpuPath = false;  // True when using external-memory (GPU) buffers

  // Per-ring-slot state (indexed by VulkanRenderContext::frameRingIndex).
  // Grown lazily; never shrunk except on resolution change or deinit.
  std::vector<FrameData> m_frameData;
  uint32_t m_width = 0;
  uint32_t m_height = 0;

  // Timeline semaphore signaled by the OIDN worker from the CPU after each
  // inference.  CB3's GPU submission waits on it so the copy from the OIDN
  // output buffer only executes after OIDN has written valid data.
  VkSemaphore m_oidnTimelineSemaphore{VK_NULL_HANDLE};
  uint64_t m_oidnSignalCounter{0};

  // Persistent worker thread + FIFO job queue (one entry per in-flight frame).
  std::thread m_oidnThread;
  std::mutex m_workerMutex;
  std::condition_variable m_workerCv;
  std::queue<std::unique_ptr<WorkerJob>> m_jobQueue;
  bool m_workerStop{false};
};
