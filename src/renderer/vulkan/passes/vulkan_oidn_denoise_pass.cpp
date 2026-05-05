#include "vulkan_oidn_denoise_pass.hpp"

#include <cuda_runtime_api.h>

#include <cstring>
#include <limits>
#include <stdexcept>

#include "backend/vulkan/core/vulkan_render_context.hpp"
#include "nvvk/check_error.hpp"
#include "nvvk/gbuffers.hpp"

#ifdef None
#undef None
#endif

// ---------------------------------------------------------------------------
// File-local helpers
// ---------------------------------------------------------------------------

namespace
{

uint32_t findMemoryType(VkPhysicalDevice physDevice, uint32_t typeFilter,
                        VkMemoryPropertyFlags props)
{
  VkPhysicalDeviceMemoryProperties memProps;
  vkGetPhysicalDeviceMemoryProperties(physDevice, &memProps);
  for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
  {
    if ((typeFilter & (1u << i)) &&
        (memProps.memoryTypes[i].propertyFlags & props) == props)
    {
      return i;
    }
  }
  return UINT32_MAX;
}

// Pixel stride in bytes for VK_FORMAT_R32G32B32A32_SFLOAT.
constexpr VkDeviceSize kBytesPerPixel = 4 * sizeof(float);

}  // namespace

// ===========================================================================
// Constructor
// ===========================================================================

/**********************************************************/
OIDNDenoisePass::OIDNDenoisePass(
    VulkanContextManager* contextManager,
    VulkanFrameSynchronizationManager* frameSyncManager) :
    m_contextManager(contextManager), m_frameSyncManager(frameSyncManager)
/**********************************************************/
{
}

// ===========================================================================
// Lifecycle
// ===========================================================================

/**********************************************************/
void OIDNDenoisePass::init()
/**********************************************************/
{
  m_numFrames = m_frameSyncManager->getFrameCycleSize();
  m_frameResources.resize(m_numFrames);

  // 1. Create per-slot CUDA streams (sets m_gpuPath).
  //    Also determines whether the GPU path is viable.
  createCudaStreams();

  // 2. Create the two exportable Vulkan timeline semaphores used for
  //    Vulkan ↔ CUDA synchronisation (or CPU vkSignalSemaphore fallback).
  createSemaphores();

  // 3. GPU path: import the semaphores into CUDA.
  //    On failure createCudaResources() clears m_gpuPath.
  if (m_gpuPath)
  {
    createCudaResources();
    if (!m_gpuPath)
    {
      // createCudaResources() cleared m_gpuPath on failure; destroy all CUDA
      // resources (streams + any partially-imported external semaphores).
      destroyCudaResources();
    }
  }

  // 4. Create one committed OIDN device per frame-ring slot.  Each GPU-path
  //    device is permanently bound to its own CUDA stream before commit() so
  //    that executeAsync() can dispatch without any per-frame device mutation.
  createOIDNDevices();

  // 5. Allocate the dedicated command pool for post-denoise command buffers.
  //    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT lets each buffer be
  //    reset individually in execute() without touching the frame's own pool.
  {
    const VkCommandPoolCreateInfo poolCI{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_contextManager->getQueueInfo(0).familyIndex,
    };
    NVVK_CHECK(vkCreateCommandPool(m_contextManager->getDevice(), &poolCI,
                                   nullptr, &m_postCmdPool));
  }

  // 6. Pre-allocate one post-command buffer per frame-ring slot.
  for (auto& fr : m_frameResources)
  {
    const VkCommandBufferAllocateInfo allocCI{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_postCmdPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    NVVK_CHECK(vkAllocateCommandBuffers(m_contextManager->getDevice(), &allocCI,
                                        &fr.postCmdBuf));
  }
}

/**********************************************************/
void OIDNDenoisePass::deinit()
/**********************************************************/
{
  // Release OIDN filters and buffers before the devices.
  destroyFrameResources();

  // Destroy the post-command pool (implicitly frees all postCmdBufs).
  if (m_postCmdPool != VK_NULL_HANDLE)
  {
    vkDestroyCommandPool(m_contextManager->getDevice(), m_postCmdPool, nullptr);
    m_postCmdPool = VK_NULL_HANDLE;
  }

  // Release per-slot OIDN devices after their filters and buffers.
  for (auto& fr : m_frameResources)
  {
    fr.oidnDevice = oidn::DeviceRef{};
  }

  destroyCudaResources();
  destroySemaphores();
}

// ===========================================================================
// Setup
// ===========================================================================

/**********************************************************/
void OIDNDenoisePass::setup(PassBuilder& builder)
/**********************************************************/
{
  builder.read(RenderOutput::Linear, PipelineStage::Transfer,
               ResourceState::TransferSrc);
  builder.read(RenderOutput::Albedo, PipelineStage::Transfer,
               ResourceState::TransferSrc);
  builder.read(RenderOutput::Normal, PipelineStage::Transfer,
               ResourceState::TransferSrc);
  builder.write(RenderOutput::Denoised, PipelineStage::Transfer,
                ResourceState::TransferDst);
}

// ===========================================================================
// Execute  —  three-part split-command-buffer model
// ===========================================================================

/**********************************************************/
void OIDNDenoisePass::execute(IRenderContext& ctx)
/**********************************************************/
{
  VulkanRenderContext& vkCtx = VulkanRenderContext::get(ctx);
  const uint32_t frameIdx = m_frameSyncManager->getCurrentFrameIndex();
  FrameResources& fr = m_frameResources[frameIdx];

  const nvvk::GBuffer* gBuffers = vkCtx.gBuffers;
  const VkExtent2D size = gBuffers->getSize();

  // Recreate per-frame buffers when the resolution changes.
  if (size.width != m_width || size.height != m_height)
  {
    m_contextManager->waitForDeviceIdle();
    destroyFrameResources();
    createFrameResources(size.width, size.height);
    m_width = size.width;
    m_height = size.height;
  }

  if (fr.colorBuf.buffer == VK_NULL_HANDLE)
  {
    return;  // Allocation failed; denoising skipped.
  }

  // -------------------------------------------------------------------------
  // Part 1: Pre-Denoise (Vulkan)
  // Record GBuffer → OIDN-input-buffer copies into the current command buffer,
  // then end it.  For the GPU path it is registered in preCommandBuffers so
  // endFrame() submits it (signalling Semaphore A) before the main submit.
  // For the CPU path it is submitted immediately here so the CPU can block on
  // Semaphore A and run OIDN synchronously.
  // -------------------------------------------------------------------------
  VkCommandBuffer preCmdBuf = vkCtx.cmdBuffer;

  {
    auto copyImageToBuffer = [&](RenderOutput src, VkBuffer dst)
    {
      VkBufferImageCopy region{};
      region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.imageSubresource.layerCount = 1;
      region.imageExtent = {size.width, size.height, 1};
      vkCmdCopyImageToBuffer(preCmdBuf, gBuffers->getColorImage(src),
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, 1,
                             &region);
    };

    copyImageToBuffer(RenderOutput::Linear, fr.colorBuf.buffer);
    copyImageToBuffer(RenderOutput::Albedo, fr.albedoBuf.buffer);
    copyImageToBuffer(RenderOutput::Normal, fr.normalBuf.buffer);
  }

  NVVK_CHECK(vkEndCommandBuffer(preCmdBuf));

  // Use the frame's timeline value as the semaphore signal/wait value.
  // vkCtx.frameNumber is advanced by m_numFrames each cycle in beginFrame(),
  // so it is strictly monotone within each slot's independent semaphore pair.
  const uint64_t semValue = vkCtx.frameNumber;

  // -------------------------------------------------------------------------
  // Part 2: Denoise
  // GPU path: enqueue — on this slot's own CUDA stream — a semaphore wait,
  //           async OIDN execution, then a semaphore signal (all non-blocking
  //           on the CPU).  Because each slot has a dedicated CUDA stream and
  //           a dedicated semaphore pair, the GPU can run OIDN for different
  //           slots concurrently.
  // CPU path: block until the per-slot Semaphore A is signalled, run OIDN
  //           synchronously, then signal Semaphore B from the CPU.
  // -------------------------------------------------------------------------
  if (m_gpuPath && !m_cudaStreams.empty())
  {
    auto stream  = static_cast<cudaStream_t>(m_cudaStreams[frameIdx]);
    auto extSemA = reinterpret_cast<cudaExternalSemaphore_t>(
        m_cudaExtSemVulkanToCuda[frameIdx]);
    auto extSemB = reinterpret_cast<cudaExternalSemaphore_t>(
        m_cudaExtSemCudaToVulkan[frameIdx]);

    // Enqueue CUDA work first (non-blocking on CPU).  The CUDA driver will
    // not start executing until the GPU signals Semaphore A, which happens
    // when the pre-command buffer is submitted by endFrame().
    cudaExternalSemaphoreWaitParams waitParams{};
    waitParams.params.fence.value = semValue;
    waitParams.flags              = 0;
    if (cudaWaitExternalSemaphoresAsync(&extSemA, &waitParams, 1, stream) != cudaSuccess)
    {
      fprintf(stderr, "[OIDNDenoisePass] cudaWaitExternalSemaphoresAsync failed: %s\n",
              cudaGetErrorString(cudaGetLastError()));
    }

    fr.filter.executeAsync();

    // Enqueue: signal m_semCudaToVulkan[frameIdx] so the final submit can proceed.
    // If this fails the GPU finalSubmit will deadlock; fall back to CPU signal.
    cudaExternalSemaphoreSignalParams signalParams{};
    signalParams.params.fence.value = semValue;
    signalParams.flags              = 0;
    if (cudaSignalExternalSemaphoresAsync(&extSemB, &signalParams, 1, stream) !=
        cudaSuccess)
    {
      fprintf(stderr, "[OIDNDenoisePass] cudaSignalExternalSemaphoresAsync failed: %s; "
                      "signalling semaphore from CPU to prevent deadlock.\n",
              cudaGetErrorString(cudaGetLastError()));
      const VkSemaphoreSignalInfo fallbackSignal{
          .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
          .semaphore = m_semCudaToVulkan[frameIdx],
          .value     = semValue,
      };
      vkSignalSemaphore(m_contextManager->getDevice(), &fallbackSignal);
    }

    // Register the pre-command buffer (GBuffer → OIDN buffer copies) with the
    // render context.  endFrame() will submit it — signalling Semaphore A —
    // before the main command buffer.  This avoids a mid-frame vkQueueSubmit2.
    vkCtx.preCommandBuffers.push_back(
        {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
         .commandBuffer = preCmdBuf});
    vkCtx.preSignalSemaphores.push_back(
        {.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
         .semaphore = m_semVulkanToCuda[frameIdx],
         .value     = semValue,
         .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT});
  }
  else
  {
    // CPU fallback: the pre-command buffer must be submitted immediately and
    // waited upon synchronously before OIDN can run.  Do the submit here
    // rather than deferring to endFrame(), which cannot block mid-frame.
    const VkCommandBufferSubmitInfo preCmdInfo{
        .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = preCmdBuf,
    };
    const VkSemaphoreSubmitInfo signalSemA{
        .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = m_semVulkanToCuda[frameIdx],
        .value     = semValue,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    const VkSubmitInfo2 preSubmit{
        .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount   = 1,
        .pCommandBufferInfos      = &preCmdInfo,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos    = &signalSemA,
    };
    NVVK_CHECK(vkQueueSubmit2(m_contextManager->getQueueInfo(0).queue, 1,
                              &preSubmit, VK_NULL_HANDLE));

    // Block until m_semVulkanToCuda[frameIdx] is signalled.
    const VkSemaphoreWaitInfo waitInfo{
        .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores    = &m_semVulkanToCuda[frameIdx],
        .pValues        = &semValue,
    };
    NVVK_CHECK(vkWaitSemaphores(m_contextManager->getDevice(), &waitInfo,
                                std::numeric_limits<uint64_t>::max()));

    fr.filter.execute();

    const char* errMsg = nullptr;
    if (fr.oidnDevice.getError(errMsg) != oidn::Error::None)
    {
      fprintf(stderr, "[OIDNDenoisePass] OIDN error: %s\n", errMsg);
    }

    // Advance m_semCudaToVulkan[frameIdx] from the CPU so the post-submit's
    // wait is satisfied immediately when the frame-sync manager submits.
    const VkSemaphoreSignalInfo signalInfo{
        .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
        .semaphore = m_semCudaToVulkan[frameIdx],
        .value     = semValue,
    };
    NVVK_CHECK(vkSignalSemaphore(m_contextManager->getDevice(), &signalInfo));
  }

  // -------------------------------------------------------------------------
  // Part 3: Post-Denoise (Vulkan)
  // Reset the recycled per-frame command buffer (GPU completion for this
  // frame slot is guaranteed by waitForFrameCompletion() in beginFrame()),
  // record the barrier and output copy, then hand it back to the engine.
  // -------------------------------------------------------------------------
  NVVK_CHECK(vkResetCommandBuffer(fr.postCmdBuf, 0));

  {
    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    NVVK_CHECK(vkBeginCommandBuffer(fr.postCmdBuf, &beginInfo));
  }

  // Memory barrier: ensure all CUDA writes to the output buffer are visible
  // before the subsequent copy.
  {
    const VkMemoryBarrier2 memBarrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
    };
    const VkDependencyInfo depInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &memBarrier,
    };
    vkCmdPipelineBarrier2(fr.postCmdBuf, &depInfo);
  }

  copyBufferToDenoised(fr.postCmdBuf, gBuffers, size, frameIdx);

  // Tell the frame-sync manager to wait on m_semCudaToVulkan[frameIdx]
  // before its final submit.
  {
    const VkSemaphoreSubmitInfo waitSemB{
        .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = m_semCudaToVulkan[frameIdx],
        .value     = semValue,
        .stageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
    };
    m_frameSyncManager->addWaitSemaphore(waitSemB);
  }

  // Replace the context's command buffer so ToneMap, UI, and endFrame()
  // all operate on the post-denoise buffer.
  vkCtx.cmdBuffer = fr.postCmdBuf;
}

// ===========================================================================
// createCudaStreams / createOIDNDevices
// ===========================================================================

/**********************************************************/
void OIDNDenoisePass::createCudaStreams()
/**********************************************************/
{
  // Probe CUDA availability.  If the runtime is absent or no devices exist
  // we fall straight through to the CPU auto-select path.
  int cudaDeviceCount = 0;
  bool cudaAvailable = (cudaGetDeviceCount(&cudaDeviceCount) == cudaSuccess &&
                        cudaDeviceCount > 0);

  if (cudaAvailable)
  {
    // Create one dedicated CUDA stream per frame-ring slot.  Using independent
    // per-slot streams removes the inter-slot serialisation that a single
    // shared stream imposes: the GPU can execute OIDN for slot N concurrently
    // with OIDN for slot N+1 when they sit on different streams.
    m_cudaStreams.resize(m_numFrames, nullptr);
    bool streamsOk = true;
    for (uint32_t i = 0; i < m_numFrames; ++i)
    {
      cudaStream_t s = nullptr;
      cudaError_t err = cudaStreamCreate(&s);
      if (err != cudaSuccess)
      {
        fprintf(stderr, "[OIDNDenoisePass] cudaStreamCreate failed for slot %u: %s\n",
                i, cudaGetErrorString(err));
        streamsOk = false;
        break;
      }
      m_cudaStreams[i] = static_cast<void*>(s);
    }

    if (streamsOk)
    {
      m_gpuPath = true;
      return;
    }

    fprintf(stderr, "[OIDNDenoisePass] cudaStreamCreate failed; "
                    "falling back to CPU OIDN path.\n");
    for (auto& s : m_cudaStreams)
    {
      if (s != nullptr)
      {
        cudaError_t err = cudaStreamDestroy(static_cast<cudaStream_t>(s));
        if (err != cudaSuccess)
        {
          fprintf(stderr, "[OIDNDenoisePass] cudaStreamDestroy failed: %s\n",
                  cudaGetErrorString(err));
        }
      }
    }
    m_cudaStreams.clear();
  }

  m_gpuPath = false;
}

/**********************************************************/
void OIDNDenoisePass::createOIDNDevices()
/**********************************************************/
{
  // Helper that creates CPU-fallback devices for all slots.
  auto createCpuDevices = [&]() {
    for (auto& fr : m_frameResources)
    {
      fr.oidnDevice = oidn::newDevice();
      fr.oidnDevice.commit();
    }
  };

  if (!m_gpuPath)
  {
    createCpuDevices();
    return;
  }

  // GPU path: create one CUDA OIDN device per slot and permanently bind it
  // to that slot's stream before calling commit().  This avoids the per-frame
  // device mutation (set("cudaStream", ...)) that caused implicit stalls.
  for (uint32_t i = 0; i < m_numFrames; ++i)
  {
    auto& fr = m_frameResources[i];
    fr.oidnDevice = oidn::newDevice(oidn::DeviceType::CUDA);
    fr.oidnDevice.set("cudaStream", m_cudaStreams[i]);
    fr.oidnDevice.commit();

    const char* errMsg = nullptr;
    if (fr.oidnDevice.getError(errMsg) != oidn::Error::None)
    {
      fprintf(stderr,
              "[OIDNDenoisePass] OIDN device commit failed for slot %u (%s); "
              "falling back to CPU devices.\n",
              i, errMsg ? errMsg : "unknown");
      // Release all partially-created devices and fall back entirely to CPU.
      for (auto& fr2 : m_frameResources)
        fr2.oidnDevice = oidn::DeviceRef{};
      destroyCudaResources();
      m_gpuPath = false;
      createCpuDevices();
      return;
    }
  }
}

// ===========================================================================
// createSemaphores / destroySemaphores
// ===========================================================================

// Helper that creates a single exportable Vulkan timeline semaphore.
static VkSemaphore createExportableTimelineSemaphore(VkDevice device)
{
#ifdef _WIN32
  constexpr VkExternalSemaphoreHandleTypeFlagBits kHandleType =
      VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
  constexpr VkExternalSemaphoreHandleTypeFlagBits kHandleType =
      VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
  const VkExportSemaphoreCreateInfo exportCI{
      .sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
      .handleTypes = kHandleType,
  };
  const VkSemaphoreTypeCreateInfo typeCI{
      .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .pNext         = &exportCI,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue  = 0,
  };
  const VkSemaphoreCreateInfo semCI{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &typeCI,
  };
  VkSemaphore sem = VK_NULL_HANDLE;
  NVVK_CHECK(vkCreateSemaphore(device, &semCI, nullptr, &sem));
  return sem;
}

/**********************************************************/
void OIDNDenoisePass::createSemaphores()
/**********************************************************/
{
  VkDevice device = m_contextManager->getDevice();
  m_semVulkanToCuda.resize(m_numFrames, VK_NULL_HANDLE);
  m_semCudaToVulkan.resize(m_numFrames, VK_NULL_HANDLE);
  for (uint32_t i = 0; i < m_numFrames; ++i)
  {
    m_semVulkanToCuda[i] = createExportableTimelineSemaphore(device);
    m_semCudaToVulkan[i] = createExportableTimelineSemaphore(device);
  }
}

/**********************************************************/
void OIDNDenoisePass::destroySemaphores()
/**********************************************************/
{
  VkDevice device = m_contextManager->getDevice();
  for (auto& sem : m_semVulkanToCuda)
  {
    if (sem != VK_NULL_HANDLE)
    {
      vkDestroySemaphore(device, sem, nullptr);
      sem = VK_NULL_HANDLE;
    }
  }
  m_semVulkanToCuda.clear();
  for (auto& sem : m_semCudaToVulkan)
  {
    if (sem != VK_NULL_HANDLE)
    {
      vkDestroySemaphore(device, sem, nullptr);
      sem = VK_NULL_HANDLE;
    }
  }
  m_semCudaToVulkan.clear();
}

// ===========================================================================
// createCudaResources / destroyCudaResources
// ===========================================================================

/**********************************************************/
void OIDNDenoisePass::createCudaResources()
/**********************************************************/
{
  VkDevice device = m_contextManager->getDevice();

  // Export each per-slot Vulkan timeline semaphore and import it into CUDA.
  // Having one CUDA external semaphore per slot means each slot's CUDA stream
  // can wait/signal independently without cross-slot ordering constraints.
#ifdef _WIN32
  if (vkGetSemaphoreWin32HandleKHR == nullptr)
  {
    fprintf(stderr,
            "[OIDNDenoisePass] vkGetSemaphoreWin32HandleKHR not loaded; "
            "disabling GPU path.\n");
    m_gpuPath = false;
    return;
  }

  auto importWin32 = [&](VkSemaphore sem) -> void* {
    const VkSemaphoreGetWin32HandleInfoKHR info{
        .sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR,
        .semaphore  = sem,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT,
    };
    HANDLE handle = nullptr;
    NVVK_CHECK(vkGetSemaphoreWin32HandleKHR(device, &info, &handle));
    cudaExternalSemaphoreHandleDesc desc{};
    desc.type                = cudaExternalSemaphoreHandleTypeTimelineSemaphoreWin32;
    desc.handle.win32.handle = handle;
    desc.flags               = 0;
    cudaExternalSemaphore_t extSem{};
    if (cudaImportExternalSemaphore(&extSem, &desc) != cudaSuccess)
    {
      fprintf(stderr, "[OIDNDenoisePass] Win32: cudaImportExternalSemaphore failed: %s\n",
              cudaGetErrorString(cudaGetLastError()));
      return nullptr;
    }
    return reinterpret_cast<void*>(extSem);
  };

  m_cudaExtSemVulkanToCuda.resize(m_numFrames, nullptr);
  m_cudaExtSemCudaToVulkan.resize(m_numFrames, nullptr);
  for (uint32_t i = 0; i < m_numFrames; ++i)
  {
    m_cudaExtSemVulkanToCuda[i] = importWin32(m_semVulkanToCuda[i]);
    m_cudaExtSemCudaToVulkan[i] = importWin32(m_semCudaToVulkan[i]);
    if (m_cudaExtSemVulkanToCuda[i] == nullptr ||
        m_cudaExtSemCudaToVulkan[i] == nullptr)
    {
      fprintf(stderr, "[OIDNDenoisePass] cudaImportExternalSemaphore failed "
                      "for slot %u; disabling GPU path.\n", i);
      m_gpuPath = false;
      return;
    }
  }
#else
  if (vkGetSemaphoreFdKHR == nullptr)
  {
    fprintf(stderr, "[OIDNDenoisePass] vkGetSemaphoreFdKHR not loaded; "
                    "disabling GPU path.\n");
    m_gpuPath = false;
    return;
  }

  auto importFd = [&](VkSemaphore sem) -> void* {
    const VkSemaphoreGetFdInfoKHR info{
        .sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .semaphore  = sem,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
    };
    int fd = -1;
    NVVK_CHECK(vkGetSemaphoreFdKHR(device, &info, &fd));
    cudaExternalSemaphoreHandleDesc desc{};
    desc.type      = cudaExternalSemaphoreHandleTypeTimelineSemaphoreFd;
    desc.handle.fd = fd;
    desc.flags     = 0;
    cudaExternalSemaphore_t extSem{};
    if (cudaImportExternalSemaphore(&extSem, &desc) != cudaSuccess)
    {
      fprintf(stderr, "[OIDNDenoisePass] FD: cudaImportExternalSemaphore failed: %s\n",
              cudaGetErrorString(cudaGetLastError()));
      return nullptr;
    }
    return reinterpret_cast<void*>(extSem);
  };

  m_cudaExtSemVulkanToCuda.resize(m_numFrames, nullptr);
  m_cudaExtSemCudaToVulkan.resize(m_numFrames, nullptr);
  for (uint32_t i = 0; i < m_numFrames; ++i)
  {
    m_cudaExtSemVulkanToCuda[i] = importFd(m_semVulkanToCuda[i]);
    m_cudaExtSemCudaToVulkan[i] = importFd(m_semCudaToVulkan[i]);
    if (m_cudaExtSemVulkanToCuda[i] == nullptr ||
        m_cudaExtSemCudaToVulkan[i] == nullptr)
    {
      fprintf(stderr, "[OIDNDenoisePass] cudaImportExternalSemaphore failed "
                      "for slot %u; disabling GPU path.\n", i);
      m_gpuPath = false;
      return;
    }
  }
#endif
}

/**********************************************************/
void OIDNDenoisePass::destroyCudaResources()
/**********************************************************/
{
  for (auto& extSem : m_cudaExtSemVulkanToCuda)
  {
    if (extSem != nullptr)
    {
      cudaDestroyExternalSemaphore(
          reinterpret_cast<cudaExternalSemaphore_t>(extSem));
    }
  }
  m_cudaExtSemVulkanToCuda.clear();

  for (auto& extSem : m_cudaExtSemCudaToVulkan)
  {
    if (extSem != nullptr)
    {
      cudaDestroyExternalSemaphore(
          reinterpret_cast<cudaExternalSemaphore_t>(extSem));
    }
  }
  m_cudaExtSemCudaToVulkan.clear();

  for (auto& stream : m_cudaStreams)
  {
    if (stream != nullptr)
      cudaStreamDestroy(static_cast<cudaStream_t>(stream));
  }
  m_cudaStreams.clear();
}

// ===========================================================================
// Per-frame resource management
// ===========================================================================

/**********************************************************/
void OIDNDenoisePass::createFrameResources(uint32_t width, uint32_t height)
/**********************************************************/
{
  const size_t byteSize = static_cast<size_t>(width) * height * kBytesPerPixel;

  constexpr VkBufferUsageFlags kInputUsage =
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  constexpr VkBufferUsageFlags kOutputUsage =
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

  for (auto& fr : m_frameResources)
  {
    if (m_gpuPath)
    {
      fr.colorBuf = allocateExternalBuffer(byteSize, kInputUsage, fr.oidnDevice);
      fr.albedoBuf = allocateExternalBuffer(byteSize, kInputUsage, fr.oidnDevice);
      fr.normalBuf = allocateExternalBuffer(byteSize, kInputUsage, fr.oidnDevice);
      fr.outputBuf = allocateExternalBuffer(byteSize, kOutputUsage, fr.oidnDevice);
    }
    else
    {
      fr.colorBuf = allocateHostBuffer(byteSize, kInputUsage, fr.oidnDevice);
      fr.albedoBuf = allocateHostBuffer(byteSize, kInputUsage, fr.oidnDevice);
      fr.normalBuf = allocateHostBuffer(byteSize, kInputUsage, fr.oidnDevice);
      fr.outputBuf = allocateHostBuffer(byteSize, kOutputUsage, fr.oidnDevice);
    }

    if (fr.colorBuf.buffer == VK_NULL_HANDLE ||
        fr.albedoBuf.buffer == VK_NULL_HANDLE ||
        fr.normalBuf.buffer == VK_NULL_HANDLE ||
        fr.outputBuf.buffer == VK_NULL_HANDLE)
    {
      fprintf(stderr, "[OIDNDenoisePass] Buffer allocation failed; "
                      "denoising disabled.\n");
      destroyFrameResources();
      return;
    }
  }

  rebuildFilters(width, height);
}

/**********************************************************/
void OIDNDenoisePass::destroyFrameResources()
/**********************************************************/
{
  for (auto& fr : m_frameResources)
  {
    fr.filter = oidn::FilterRef{};  // Release before the buffers it references
    destroyBuffer(fr.colorBuf);
    destroyBuffer(fr.albedoBuf);
    destroyBuffer(fr.normalBuf);
    destroyBuffer(fr.outputBuf);
    // postCmdBuf is intentionally NOT cleared here: it is pre-allocated in
    // init(), remains valid across resolution changes, and is only freed when
    // m_postCmdPool is destroyed in deinit().
  }
  m_width = 0;
  m_height = 0;
}

/**********************************************************/
void OIDNDenoisePass::rebuildFilters(uint32_t width, uint32_t height)
/**********************************************************/
{
  const size_t bytePixelStride = kBytesPerPixel;
  const size_t byteRowStride = static_cast<size_t>(width) * bytePixelStride;

  for (auto& fr : m_frameResources)
  {
    fr.filter = fr.oidnDevice.newFilter("RT");

    fr.filter.setImage("color", fr.colorBuf.oidnBuf, oidn::Format::Float3,
                       width, height, 0, bytePixelStride, byteRowStride);
    fr.filter.setImage("albedo", fr.albedoBuf.oidnBuf, oidn::Format::Float3,
                       width, height, 0, bytePixelStride, byteRowStride);
    fr.filter.setImage("normal", fr.normalBuf.oidnBuf, oidn::Format::Float3,
                       width, height, 0, bytePixelStride, byteRowStride);
    fr.filter.setImage("output", fr.outputBuf.oidnBuf, oidn::Format::Float3,
                       width, height, 0, bytePixelStride, byteRowStride);

    fr.filter.set("hdr", true);
    fr.filter.commit();
  }
}

// ===========================================================================
// copyBufferToDenoised
// ===========================================================================

/**********************************************************/
void OIDNDenoisePass::copyBufferToDenoised(VkCommandBuffer cmd,
                                           const nvvk::GBuffer* gBuffers,
                                           VkExtent2D size, uint32_t frameIdx)
/**********************************************************/
{
  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {size.width, size.height, 1};

  vkCmdCopyBufferToImage(cmd, m_frameResources[frameIdx].outputBuf.buffer,
                         gBuffers->getColorImage(RenderOutput::Denoised),
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

// ===========================================================================
// Buffer allocation helpers  (unchanged from single-buffered version)
// ===========================================================================

/**********************************************************/
OIDNDenoisePass::ExternalBuffer
OIDNDenoisePass::allocateExternalBuffer(size_t byteSize,
                                        VkBufferUsageFlags usage,
                                        oidn::DeviceRef& oidnDevice)
/**********************************************************/
{
  ExternalBuffer buf;
  buf.byteSize = byteSize;

  VkDevice device = m_contextManager->getDevice();
  VkPhysicalDevice physDevice = m_contextManager->getPhysicalDevice();

#ifdef _WIN32
  constexpr VkExternalMemoryHandleTypeFlagBits kHandleType =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
  constexpr oidn::ExternalMemoryTypeFlag kOIDNHandleType =
      oidn::ExternalMemoryTypeFlag::OpaqueWin32;
#else
  constexpr VkExternalMemoryHandleTypeFlagBits kHandleType =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
  constexpr oidn::ExternalMemoryTypeFlag kOIDNHandleType =
      oidn::ExternalMemoryTypeFlag::OpaqueFD;
#endif

  // Verify the physical device can export this handle type for buffers.
  VkPhysicalDeviceExternalBufferInfo extBufQuery{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO};
  extBufQuery.usage = usage;
  extBufQuery.handleType = kHandleType;

  VkExternalBufferProperties extBufProps{
      VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
  vkGetPhysicalDeviceExternalBufferProperties(physDevice, &extBufQuery,
                                              &extBufProps);

  if (!(extBufProps.externalMemoryProperties.externalMemoryFeatures &
        VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT))
  {
    return allocateHostBuffer(byteSize, usage, oidnDevice);
  }

  // Create VkBuffer with external-memory export flag.
  const VkExternalMemoryBufferCreateInfo extBufInfo{
      VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO, nullptr,
      kHandleType};

  VkBufferCreateInfo bufCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufCI.pNext = &extBufInfo;
  bufCI.size = byteSize;
  bufCI.usage = usage;
  bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  NVVK_CHECK(vkCreateBuffer(device, &bufCI, nullptr, &buf.buffer));

  VkMemoryRequirements memReqs;
  vkGetBufferMemoryRequirements(device, buf.buffer, &memReqs);

  const uint32_t memTypeIdx = findMemoryType(
      physDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (memTypeIdx == UINT32_MAX)
  {
    vkDestroyBuffer(device, buf.buffer, nullptr);
    buf.buffer = VK_NULL_HANDLE;
    return allocateHostBuffer(byteSize, usage, oidnDevice);
  }

  const VkExportMemoryAllocateInfo exportInfo{
      VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, nullptr, kHandleType};

  VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocInfo.pNext = &exportInfo;
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = memTypeIdx;

  NVVK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &buf.memory));
  NVVK_CHECK(vkBindBufferMemory(device, buf.buffer, buf.memory, 0));

  // Export the memory handle and import it into the OIDN device.
#ifdef _WIN32
  const VkMemoryGetWin32HandleInfoKHR getHandleInfo{
      VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR, nullptr, buf.memory,
      kHandleType};
  HANDLE win32Handle = nullptr;
  NVVK_CHECK(vkGetMemoryWin32HandleKHR(device, &getHandleInfo, &win32Handle));
  buf.oidnBuf =
      oidnDevice.newBuffer(kOIDNHandleType, win32Handle, nullptr, byteSize);
#else
  const VkMemoryGetFdInfoKHR getFdInfo{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
                                       nullptr, buf.memory, kHandleType};
  int fd = -1;
  if (vkGetMemoryFdKHR == nullptr)
  {
    throw std::runtime_error("[OIDNDenoisePass] vkGetMemoryFdKHR is NULL — "
                             "VK_KHR_external_memory_fd extension not loaded.");
  }
  NVVK_CHECK(vkGetMemoryFdKHR(device, &getFdInfo, &fd));
  buf.oidnBuf = oidnDevice.newBuffer(kOIDNHandleType, fd, byteSize);
#endif

  return buf;
}

/**********************************************************/
OIDNDenoisePass::ExternalBuffer
OIDNDenoisePass::allocateHostBuffer(size_t byteSize, VkBufferUsageFlags usage,
                                    oidn::DeviceRef& oidnDevice)
/**********************************************************/
{
  ExternalBuffer buf;
  buf.byteSize = byteSize;

  VkDevice device = m_contextManager->getDevice();
  VkPhysicalDevice physDevice = m_contextManager->getPhysicalDevice();

  VkBufferCreateInfo bufCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufCI.size = byteSize;
  bufCI.usage = usage;
  bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  NVVK_CHECK(vkCreateBuffer(device, &bufCI, nullptr, &buf.buffer));

  VkMemoryRequirements memReqs;
  vkGetBufferMemoryRequirements(device, buf.buffer, &memReqs);

  const uint32_t memTypeIdx =
      findMemoryType(physDevice, memReqs.memoryTypeBits,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (memTypeIdx == UINT32_MAX)
  {
    fprintf(stderr,
            "[OIDNDenoisePass] Could not find a host-visible memory type — "
            "OIDN CPU fallback unavailable.\n");
    vkDestroyBuffer(device, buf.buffer, nullptr);
    buf.buffer = VK_NULL_HANDLE;
    return buf;
  }

  VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = memTypeIdx;

  NVVK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &buf.memory));
  NVVK_CHECK(vkBindBufferMemory(device, buf.buffer, buf.memory, 0));
  NVVK_CHECK(vkMapMemory(device, buf.memory, 0, byteSize, 0, &buf.hostPtr));

  buf.oidnBuf = oidnDevice.newBuffer(buf.hostPtr, byteSize);
  return buf;
}

/**********************************************************/
void OIDNDenoisePass::destroyBuffer(ExternalBuffer& buf)
/**********************************************************/
{
  buf.oidnBuf = oidn::BufferRef{};  // Release OIDN side first

  VkDevice device = m_contextManager->getDevice();
  if (buf.hostPtr != nullptr)
  {
    vkUnmapMemory(device, buf.memory);
    buf.hostPtr = nullptr;
  }
  if (buf.memory != VK_NULL_HANDLE)
  {
    vkFreeMemory(device, buf.memory, nullptr);
    buf.memory = VK_NULL_HANDLE;
  }
  if (buf.buffer != VK_NULL_HANDLE)
  {
    vkDestroyBuffer(device, buf.buffer, nullptr);
    buf.buffer = VK_NULL_HANDLE;
  }
  buf.byteSize = 0;
}
