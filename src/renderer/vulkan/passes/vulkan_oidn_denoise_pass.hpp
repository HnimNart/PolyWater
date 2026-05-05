#pragma once

#include <vulkan/vulkan_core.h>

#ifdef None
#undef None
#endif
#include <OpenImageDenoise/oidn.hpp>
#include <nvvk/gbuffers.hpp>

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
// Execution model
// ---------------
//  1. The render-graph inserts barriers transitioning the three input images
//     to TRANSFER_SRC and the output image to TRANSFER_DST, then calls
//     execute().
//  2. execute() records vkCmdCopyImageToBuffer commands for the three inputs
//     into the current (pre-OIDN) command buffer.
//  3. That command buffer is ended and submitted to the graphics queue with
//     an internal fence; the CPU waits for it.
//  4. OIDN executes on the GPU (the input Vulkan buffers are shared with the
//     OIDN device via Vulkan external-memory handles).
//  5. After oidnDevice.sync(), a fresh command buffer is started from the
//     same pool, records the copy from the OIDN output buffer back to the
//     Denoised G-buffer image, and is stored back in the IRenderContext.
//     Subsequent passes (ToneMap, UI) record into this new command buffer,
//     which the frame-sync manager ends and submits at end-of-frame.
// ---------------------------------------------------------------------------
class OIDNDenoisePass final : public IRenderPass
{
public:
  explicit OIDNDenoisePass(VulkanContextManager* contextManager);

  void init() override;
  void deinit() override;
  void setup(PassBuilder& builder) override;
  void execute(IRenderContext& ctx) override;

  // The OIDN denoiser runs in a dedicated slot so the main ray-trace
  // command buffer can be submitted and waited on before OIDN executes.
  PassCmdSlot cmdSlot() const override { return PassCmdSlot::Denoise; }

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

  // -----------------------------------------------------------------------
  // Helpers
  // -----------------------------------------------------------------------
  void createOIDNDevice();
  void createBuffers(uint32_t width, uint32_t height);
  void destroyBuffers();
  void rebuildFilter(uint32_t width, uint32_t height);

  // GPU path: allocate a Vulkan buffer backed by exportable device-local
  // memory and import it into the OIDN device as a shared buffer.
  ExternalBuffer allocateExternalBuffer(size_t byteSize,
                                        VkBufferUsageFlags usage);

  // CPU / host-visible fallback: allocate a HOST_VISIBLE | HOST_COHERENT
  // buffer and wrap it in an OIDN shared buffer backed by the mapped ptr.
  ExternalBuffer allocateHostBuffer(size_t byteSize, VkBufferUsageFlags usage);
  void destroyBuffer(ExternalBuffer& buf);
  void copyBufferToDenoised(VkCommandBuffer cmd, const nvvk::GBuffer* gBuffers,
                            VkExtent2D size);

  // -----------------------------------------------------------------------
  // Members
  // -----------------------------------------------------------------------
  VulkanContextManager* m_contextManager = nullptr;
  VkFence m_fence = VK_NULL_HANDLE;

  oidn::DeviceRef m_oidnDevice;
  oidn::FilterRef m_filter;

  bool m_gpuPath = false;  // True when using external-memory (GPU) buffers

  ExternalBuffer m_colorBuf;   // OIDN input  – noisy colour (Linear)
  ExternalBuffer m_albedoBuf;  // OIDN input  – first-hit albedo
  ExternalBuffer m_normalBuf;  // OIDN input  – first-hit normal
  ExternalBuffer m_outputBuf;  // OIDN output – denoised colour → Denoised

  uint32_t m_width = 0;
  uint32_t m_height = 0;
};
