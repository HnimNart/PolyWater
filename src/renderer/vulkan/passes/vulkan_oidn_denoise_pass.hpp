#pragma once

#include <vulkan/vulkan_core.h>

#ifdef None
#undef None
#endif
#include <OpenImageDenoise/oidn.hpp>
#include <nvvk/gbuffers.hpp>

#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

#include "backend/vulkan/core/vulkan_context_manager.hpp"
#include "renderer/interfaces/render_graph_interface.hpp"

// ---------------------------------------------------------------------------
// OIDNDenoisePass
//
// GPU-side AI denoiser using Intel Open Image Denoise (OIDN) v2.4+.
//
// The pass reads the noisy Linear colour buffer together with the first-hit
// Albedo and Normal G-buffers produced by RayTracePass, runs the OIDN "RT"
// beauty filter on a GPU device (CUDA/HIP/SYCL auto-selected; CPU fallback),
// and writes the result to the Denoised G-buffer.
//
// Execution model (1-frame pipelined)
// ------------------------------------
//  Two sets of OIDN buffers (slots 0 and 1) are allocated and alternate each
//  frame ("ping-pong").
//
//  Frame N:
//   1. Wait for frame N-1's OIDN future.  Because OIDN has been running on
//      the worker thread while this frame's GPU render pass executed, this
//      wait is typically zero (or very short).
//   2. Record vkCmdCopyImageToBuffer commands for the three GBuffer inputs
//      into slot N%2's input buffers.
//   3. End and submit the command buffer with a fence so the GPU can DMA the
//      data into the OIDN input buffers.
//   4. Post a job to the persistent worker thread (fence + filter ref for
//      slot N%2).  The worker waits for the fence and then calls
//      executeAsync() + sync() — but the main render thread does NOT block.
//   5. Allocate a fresh command buffer.  Record the copy from slot (N-1)%2's
//      output buffer into the Denoised GBuffer image.  This is the result of
//      the previous frame's OIDN run, which is already done (step 1).
//   6. Subsequent passes (ToneMap, UI) record into this new command buffer.
//
//  The first frame blocks synchronously so there is a valid denoised image
//  to display on frame 1.
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

  // One ping-pong slot: four OIDN buffers + the filter committed to them.
  struct FrameData
  {
    ExternalBuffer colorBuf;
    ExternalBuffer albedoBuf;
    ExternalBuffer normalBuf;
    ExternalBuffer outputBuf;
    oidn::FilterRef filter;
  };

  // Per-job context handed to the worker thread so it operates on the
  // correct slot's filter without touching shared mutable state.
  struct WorkerJob
  {
    VkFence fence;
    VkDevice device;
    oidn::FilterRef filter;
    oidn::DeviceRef oidnDevice;
    std::promise<void> completion;
  };

  // -----------------------------------------------------------------------
  // Helpers
  // -----------------------------------------------------------------------
  void createOIDNDevice();
  void createBuffers(uint32_t width, uint32_t height);
  void destroyBuffers();
  // Rebuild (and commit) the OIDN filter for the given ping-pong slot.
  void rebuildFilter(int slot, uint32_t width, uint32_t height);

  // GPU path: allocate a Vulkan buffer backed by exportable device-local
  // memory and import it into the OIDN device as a shared buffer.
  ExternalBuffer allocateExternalBuffer(size_t byteSize,
                                        VkBufferUsageFlags usage);

  // CPU / host-visible fallback: allocate a HOST_VISIBLE | HOST_COHERENT
  // buffer and wrap it in an OIDN shared buffer backed by the mapped ptr.
  ExternalBuffer allocateHostBuffer(size_t byteSize, VkBufferUsageFlags usage);
  void destroyBuffer(ExternalBuffer& buf);
  void copyBufferToDenoised(VkCommandBuffer cmd, const nvvk::GBuffer* gBuffers,
                            VkExtent2D size, VkBuffer outputBuffer);

  void oidnWorkerLoop();

  // -----------------------------------------------------------------------
  // Members
  // -----------------------------------------------------------------------
  VulkanContextManager* m_contextManager = nullptr;
  VkFence m_fence = VK_NULL_HANDLE;

  oidn::DeviceRef m_oidnDevice;

  bool m_gpuPath = false;  // True when using external-memory (GPU) buffers

  // Two ping-pong slots so that frame N's OIDN runs while frame N+1 renders.
  FrameData m_frameData[2];
  int m_pingPong = 0;  // Index of the slot being written this frame

  uint32_t m_width = 0;
  uint32_t m_height = 0;

  // Pipelining state
  std::future<void> m_prevOidnFuture;  // Future for the previous frame's OIDN job
  bool m_firstFrame = true;            // True until the first OIDN run completes
  bool m_prevSlotHasOutput = false;    // True once slot (m_pingPong^1) holds a valid denoised image

  // Worker thread for async OIDN execution
  std::thread m_oidnThread;
  std::mutex m_workerMutex;
  std::condition_variable m_workerCv;
  std::unique_ptr<WorkerJob> m_pendingJob;
  bool m_workerStop = false;
};
