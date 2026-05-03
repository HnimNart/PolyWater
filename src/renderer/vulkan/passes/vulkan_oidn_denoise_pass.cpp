#include "vulkan_oidn_denoise_pass.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>

#include <cuda_runtime_api.h>

#include "backend/vulkan/core/vulkan_render_context.hpp"
#include "nvvk/check_error.hpp"
#include "nvvk/gbuffers.hpp"

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
    VulkanContextManager*              contextManager,
    VulkanFrameSynchronizationManager* frameSyncManager)
    : m_contextManager(contextManager), m_frameSyncManager(frameSyncManager)
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

  // 1. Create the OIDN device object (no commit yet).
  //    Also creates the CUDA stream when CUDA is available.
  createOIDNDevice();

  // 2. Create the exportable Vulkan timeline semaphore used for
  //    Vulkan ↔ CUDA synchronisation (or CPU vkSignalSemaphore fallback).
  createTimelineSemaphore();

  // 3. GPU path: import the semaphore into CUDA and attach the stream to the
  //    OIDN device.  All configuration must precede commit().
  if (m_gpuPath)
  {
    createCudaResources();
    if (!m_gpuPath)
    {
      // createCudaResources() cleared m_gpuPath on failure; switch to CPU device.
      if (m_cudaStream != nullptr)
      {
        cudaStreamDestroy(static_cast<cudaStream_t>(m_cudaStream));
        m_cudaStream = nullptr;
      }
      m_oidnDevice = oidn::newDevice();
    }
    else
    {
      m_oidnDevice.set("cudaStream", m_cudaStream);
    }
  }

  // 4. Commit the device — exactly once, after all configuration.
  m_oidnDevice.commit();
  {
    const char* errMsg = nullptr;
    if (m_oidnDevice.getError(errMsg) != oidn::Error::None)
    {
      fprintf(stderr,
              "[OIDNDenoisePass] OIDN device commit failed (%s); "
              "falling back to auto-selected device.\n",
              errMsg ? errMsg : "unknown");
      destroyCudaResources();
      m_gpuPath    = false;
      m_oidnDevice = oidn::newDevice();
      m_oidnDevice.commit();
    }
  }

  // 5. Allocate the dedicated command pool for post-denoise command buffers.
  //    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT lets each buffer be
  //    reset individually in execute() without touching the frame's own pool.
  {
    const VkCommandPoolCreateInfo poolCI{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_contextManager->getQueueInfo(0).familyIndex,
    };
    NVVK_CHECK(vkCreateCommandPool(m_contextManager->getDevice(), &poolCI,
                                   nullptr, &m_postCmdPool));
  }

  // 6. Pre-allocate one post-command buffer per frame-ring slot.
  for (auto& fr : m_frameResources)
  {
    const VkCommandBufferAllocateInfo allocCI{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = m_postCmdPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
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
  // Release OIDN filters and buffers before the device.
  destroyFrameResources();

  // Destroy the post-command pool (implicitly frees all postCmdBufs).
  if (m_postCmdPool != VK_NULL_HANDLE)
  {
    vkDestroyCommandPool(m_contextManager->getDevice(), m_postCmdPool,
                         nullptr);
    m_postCmdPool = VK_NULL_HANDLE;
  }

  m_oidnDevice = oidn::DeviceRef{};

  destroyCudaResources();
  destroyTimelineSemaphore();
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
  VulkanRenderContext& vkCtx   = VulkanRenderContext::get(ctx);
  const uint32_t       frameIdx = m_frameSyncManager->getCurrentFrameIndex();
  FrameResources&      fr       = m_frameResources[frameIdx];

  const nvvk::GBuffer* gBuffers = vkCtx.gBuffers;
  const VkExtent2D     size     = gBuffers->getSize();

  // Recreate per-frame buffers when the resolution changes.
  if (size.width != m_width || size.height != m_height)
  {
    m_contextManager->waitForDeviceIdle();
    destroyFrameResources();
    createFrameResources(size.width, size.height);
    m_width  = size.width;
    m_height = size.height;
  }

  if (fr.colorBuf.buffer == VK_NULL_HANDLE)
  {
    return;  // Allocation failed; denoising skipped.
  }

  // -------------------------------------------------------------------------
  // Part 1: Pre-Denoise (Vulkan)
  // Record GBuffer → OIDN-input-buffer copies, then end and directly submit
  // this command buffer, signalling Timeline Semaphore A.
  // -------------------------------------------------------------------------
  VkCommandBuffer preCmdBuf = vkCtx.cmdBuffer;

  {
    auto copyImageToBuffer = [&](RenderOutput src, VkBuffer dst)
    {
      VkBufferImageCopy region{};
      region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.imageSubresource.layerCount = 1;
      region.imageExtent                 = {size.width, size.height, 1};
      vkCmdCopyImageToBuffer(preCmdBuf, gBuffers->getColorImage(src),
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, 1,
                             &region);
    };

    copyImageToBuffer(RenderOutput::Linear, fr.colorBuf.buffer);
    copyImageToBuffer(RenderOutput::Albedo, fr.albedoBuf.buffer);
    copyImageToBuffer(RenderOutput::Normal, fr.normalBuf.buffer);
  }

  NVVK_CHECK(vkEndCommandBuffer(preCmdBuf));

  // Advance the independent monotonic counter by 2.
  m_timelineCounter += 2;
  const uint64_t semAValue = m_timelineCounter - 1;
  const uint64_t semBValue = m_timelineCounter;

  {
    const VkCommandBufferSubmitInfo preCmdInfo{
        .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = preCmdBuf,
    };
    const VkSemaphoreSubmitInfo signalSemA{
        .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = m_timelineSemaphore,
        .value     = semAValue,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    const VkSubmitInfo2 preSubmit{
        .sType                     = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount    = 1,
        .pCommandBufferInfos       = &preCmdInfo,
        .signalSemaphoreInfoCount  = 1,
        .pSignalSemaphoreInfos     = &signalSemA,
    };
    NVVK_CHECK(vkQueueSubmit2(m_contextManager->getQueueInfo(0).queue, 1,
                              &preSubmit, VK_NULL_HANDLE));
  }

  // -------------------------------------------------------------------------
  // Part 2: Denoise
  // GPU path: enqueue CUDA semaphore wait, async OIDN execution, and
  //           semaphore signal onto the dedicated CUDA stream (non-blocking).
  // CPU path: wait on the CPU, run OIDN synchronously, signal via Vulkan.
  // -------------------------------------------------------------------------
  if (m_gpuPath && m_cudaStream != nullptr)
  {
    auto stream = static_cast<cudaStream_t>(m_cudaStream);
    auto extSem = reinterpret_cast<cudaExternalSemaphore_t>(m_cudaExtSemaphore);

    cudaExternalSemaphoreWaitParams waitParams{};
    waitParams.params.fence.value = semAValue;
    waitParams.flags              = 0;
    cudaWaitExternalSemaphoresAsync(&extSem, &waitParams, 1, stream);

    fr.filter.executeAsync();

    cudaExternalSemaphoreSignalParams signalParams{};
    signalParams.params.fence.value = semBValue;
    signalParams.flags              = 0;
    cudaSignalExternalSemaphoresAsync(&extSem, &signalParams, 1, stream);
  }
  else
  {
    // CPU fallback: block until Semaphore A is signalled, then run OIDN.
    const VkSemaphoreWaitInfo waitInfo{
        .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores    = &m_timelineSemaphore,
        .pValues        = &semAValue,
    };
    NVVK_CHECK(vkWaitSemaphores(m_contextManager->getDevice(), &waitInfo,
                                std::numeric_limits<uint64_t>::max()));

    fr.filter.execute();

    const char* errMsg = nullptr;
    if (m_oidnDevice.getError(errMsg) != oidn::Error::None)
    {
      fprintf(stderr, "[OIDNDenoisePass] OIDN error: %s\n", errMsg);
    }

    // Manually advance the semaphore to semBValue so the post-submit's wait
    // is satisfied immediately when the frame-sync manager submits.
    const VkSemaphoreSignalInfo signalInfo{
        .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
        .semaphore = m_timelineSemaphore,
        .value     = semBValue,
    };
    NVVK_CHECK(
        vkSignalSemaphore(m_contextManager->getDevice(), &signalInfo));
  }

  // -------------------------------------------------------------------------
  // Part 3: Post-Denoise (Vulkan)
  // Reset the recycled per-frame command buffer (GPU completion for this
  // frame slot is guaranteed by waitForFrameCompletion() in beginFrame()),
  // record the barrier and output copy, then hand it back to the engine.
  // -------------------------------------------------------------------------
  NVVK_CHECK(vkResetCommandBuffer(fr.postCmdBuf, 0));

  {
    // Both flags together: ONE_TIME_SUBMIT hints for driver optimisation;
    // SIMULTANEOUS_USE satisfies validation layer tracking across the
    // timeline synchronisation boundary.
    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT |
                 VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
    };
    NVVK_CHECK(vkBeginCommandBuffer(fr.postCmdBuf, &beginInfo));
  }

  // Memory barrier: ensure all CUDA writes to the output buffer are visible
  // before the subsequent copy.
  {
    const VkMemoryBarrier2 memBarrier{
        .sType        = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
    };
    const VkDependencyInfo depInfo{
        .sType                = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount   = 1,
        .pMemoryBarriers      = &memBarrier,
    };
    vkCmdPipelineBarrier2(fr.postCmdBuf, &depInfo);
  }

  copyBufferToDenoised(fr.postCmdBuf, gBuffers, size, frameIdx);

  // Tell the frame-sync manager to wait on Semaphore B before its submit.
  {
    const VkSemaphoreSubmitInfo waitSemB{
        .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = m_timelineSemaphore,
        .value     = semBValue,
        .stageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
    };
    m_frameSyncManager->addWaitSemaphore(waitSemB);
  }

  // Replace the context's command buffer so ToneMap, UI, and endFrame()
  // all operate on the post-denoise buffer.
  vkCtx.cmdBuffer = fr.postCmdBuf;
}

// ===========================================================================
// createOIDNDevice
// ===========================================================================

/**********************************************************/
void OIDNDenoisePass::createOIDNDevice()
/**********************************************************/
{
  // Probe CUDA availability.  If the runtime is absent or no devices exist
  // we fall straight through to the CPU auto-select path.
  int  cudaDeviceCount = 0;
  bool cudaAvailable   =
      (cudaGetDeviceCount(&cudaDeviceCount) == cudaSuccess &&
       cudaDeviceCount > 0);

  if (cudaAvailable)
  {
    cudaStream_t stream = nullptr;
    if (cudaStreamCreate(&stream) == cudaSuccess)
    {
      m_cudaStream = static_cast<void*>(stream);
      m_oidnDevice = oidn::newDevice(oidn::DeviceType::CUDA);
      m_gpuPath    = true;
      return;
    }
    fprintf(stderr,
            "[OIDNDenoisePass] cudaStreamCreate failed; "
            "falling back to CPU OIDN path.\n");
  }

  m_oidnDevice = oidn::newDevice();  // Auto-select (CPU)
  m_gpuPath    = false;
}

// ===========================================================================
// createTimelineSemaphore / destroyTimelineSemaphore
// ===========================================================================

/**********************************************************/
void OIDNDenoisePass::createTimelineSemaphore()
/**********************************************************/
{
  VkDevice device = m_contextManager->getDevice();

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
  NVVK_CHECK(
      vkCreateSemaphore(device, &semCI, nullptr, &m_timelineSemaphore));
}

/**********************************************************/
void OIDNDenoisePass::destroyTimelineSemaphore()
/**********************************************************/
{
  if (m_timelineSemaphore != VK_NULL_HANDLE)
  {
    vkDestroySemaphore(m_contextManager->getDevice(), m_timelineSemaphore,
                       nullptr);
    m_timelineSemaphore = VK_NULL_HANDLE;
  }
}

// ===========================================================================
// createCudaResources / destroyCudaResources
// ===========================================================================

/**********************************************************/
void OIDNDenoisePass::createCudaResources()
/**********************************************************/
{
  VkDevice device = m_contextManager->getDevice();

  // Export the Vulkan timeline semaphore and import it into CUDA so that
  // cudaWaitExternalSemaphoresAsync / cudaSignalExternalSemaphoresAsync can
  // be used to synchronise the OIDN CUDA stream with Vulkan submits.
#ifdef _WIN32
  if (vkGetSemaphoreWin32HandleKHR == nullptr)
  {
    fprintf(stderr,
            "[OIDNDenoisePass] vkGetSemaphoreWin32HandleKHR not loaded; "
            "disabling GPU path.\n");
    m_gpuPath = false;
    return;
  }
  const VkSemaphoreGetWin32HandleInfoKHR getHandleInfo{
      .sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR,
      .semaphore  = m_timelineSemaphore,
      .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT,
  };
  HANDLE win32Handle = nullptr;
  NVVK_CHECK(
      vkGetSemaphoreWin32HandleKHR(device, &getHandleInfo, &win32Handle));

  cudaExternalSemaphoreHandleDesc semDesc{};
  semDesc.type                = cudaExternalSemaphoreHandleTypeTimelineSemaphoreWin32;
  semDesc.handle.win32.handle = win32Handle;
  semDesc.flags               = 0;
#else
  if (vkGetSemaphoreFdKHR == nullptr)
  {
    fprintf(stderr,
            "[OIDNDenoisePass] vkGetSemaphoreFdKHR not loaded; "
            "disabling GPU path.\n");
    m_gpuPath = false;
    return;
  }
  const VkSemaphoreGetFdInfoKHR getFdInfo{
      .sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
      .semaphore  = m_timelineSemaphore,
      .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
  };
  int fd = -1;
  NVVK_CHECK(vkGetSemaphoreFdKHR(device, &getFdInfo, &fd));

  cudaExternalSemaphoreHandleDesc semDesc{};
  semDesc.type      = cudaExternalSemaphoreHandleTypeTimelineSemaphoreFd;
  semDesc.handle.fd = fd;
  semDesc.flags     = 0;
#endif

  cudaExternalSemaphore_t extSem{};
  if (cudaImportExternalSemaphore(&extSem, &semDesc) != cudaSuccess)
  {
    fprintf(stderr,
            "[OIDNDenoisePass] cudaImportExternalSemaphore failed; "
            "disabling GPU path.\n");
    m_gpuPath = false;
    return;
  }
  m_cudaExtSemaphore = reinterpret_cast<void*>(extSem);
}

/**********************************************************/
void OIDNDenoisePass::destroyCudaResources()
/**********************************************************/
{
  if (m_cudaExtSemaphore != nullptr)
  {
    cudaDestroyExternalSemaphore(
        reinterpret_cast<cudaExternalSemaphore_t>(m_cudaExtSemaphore));
    m_cudaExtSemaphore = nullptr;
  }
  if (m_cudaStream != nullptr)
  {
    cudaStreamDestroy(static_cast<cudaStream_t>(m_cudaStream));
    m_cudaStream = nullptr;
  }
}

// ===========================================================================
// Per-frame resource management
// ===========================================================================

/**********************************************************/
void OIDNDenoisePass::createFrameResources(uint32_t width, uint32_t height)
/**********************************************************/
{
  const size_t byteSize =
      static_cast<size_t>(width) * height * kBytesPerPixel;

  constexpr VkBufferUsageFlags kInputUsage =
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  constexpr VkBufferUsageFlags kOutputUsage =
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

  for (auto& fr : m_frameResources)
  {
    if (m_gpuPath)
    {
      fr.colorBuf  = allocateExternalBuffer(byteSize, kInputUsage);
      fr.albedoBuf = allocateExternalBuffer(byteSize, kInputUsage);
      fr.normalBuf = allocateExternalBuffer(byteSize, kInputUsage);
      fr.outputBuf = allocateExternalBuffer(byteSize, kOutputUsage);
    }
    else
    {
      fr.colorBuf  = allocateHostBuffer(byteSize, kInputUsage);
      fr.albedoBuf = allocateHostBuffer(byteSize, kInputUsage);
      fr.normalBuf = allocateHostBuffer(byteSize, kInputUsage);
      fr.outputBuf = allocateHostBuffer(byteSize, kOutputUsage);
    }

    if (fr.colorBuf.buffer == VK_NULL_HANDLE ||
        fr.albedoBuf.buffer == VK_NULL_HANDLE ||
        fr.normalBuf.buffer == VK_NULL_HANDLE ||
        fr.outputBuf.buffer == VK_NULL_HANDLE)
    {
      fprintf(stderr,
              "[OIDNDenoisePass] Buffer allocation failed; "
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
  m_width  = 0;
  m_height = 0;
}

/**********************************************************/
void OIDNDenoisePass::rebuildFilters(uint32_t width, uint32_t height)
/**********************************************************/
{
  const size_t bytePixelStride = kBytesPerPixel;
  const size_t byteRowStride   = static_cast<size_t>(width) * bytePixelStride;

  for (auto& fr : m_frameResources)
  {
    fr.filter = m_oidnDevice.newFilter("RT");

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
void OIDNDenoisePass::copyBufferToDenoised(VkCommandBuffer      cmd,
                                           const nvvk::GBuffer* gBuffers,
                                           VkExtent2D           size,
                                           uint32_t             frameIdx)
/**********************************************************/
{
  VkBufferImageCopy region{};
  region.bufferOffset                    = 0;
  region.bufferRowLength                 = 0;
  region.bufferImageHeight               = 0;
  region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel       = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount     = 1;
  region.imageOffset                     = {0, 0, 0};
  region.imageExtent                     = {size.width, size.height, 1};

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
                                        VkBufferUsageFlags usage)
/**********************************************************/
{
  ExternalBuffer buf;
  buf.byteSize = byteSize;

  VkDevice         device     = m_contextManager->getDevice();
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
  extBufQuery.usage      = usage;
  extBufQuery.handleType = kHandleType;

  VkExternalBufferProperties extBufProps{
      VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
  vkGetPhysicalDeviceExternalBufferProperties(physDevice, &extBufQuery,
                                              &extBufProps);

  if (!(extBufProps.externalMemoryProperties.externalMemoryFeatures &
        VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT))
  {
    return allocateHostBuffer(byteSize, usage);
  }

  // Create VkBuffer with external-memory export flag.
  const VkExternalMemoryBufferCreateInfo extBufInfo{
      VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
      nullptr,
      kHandleType};

  VkBufferCreateInfo bufCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufCI.pNext       = &extBufInfo;
  bufCI.size        = byteSize;
  bufCI.usage       = usage;
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
    return allocateHostBuffer(byteSize, usage);
  }

  const VkExportMemoryAllocateInfo exportInfo{
      VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, nullptr, kHandleType};

  VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocInfo.pNext           = &exportInfo;
  allocInfo.allocationSize  = memReqs.size;
  allocInfo.memoryTypeIndex = memTypeIdx;

  NVVK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &buf.memory));
  NVVK_CHECK(vkBindBufferMemory(device, buf.buffer, buf.memory, 0));

  // Export the memory handle and import it into the OIDN device.
#ifdef _WIN32
  const VkMemoryGetWin32HandleInfoKHR getHandleInfo{
      VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR, nullptr,
      buf.memory, kHandleType};
  HANDLE win32Handle = nullptr;
  NVVK_CHECK(
      vkGetMemoryWin32HandleKHR(device, &getHandleInfo, &win32Handle));
  buf.oidnBuf =
      m_oidnDevice.newBuffer(kOIDNHandleType, win32Handle, nullptr, byteSize);
#else
  const VkMemoryGetFdInfoKHR getFdInfo{
      VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR, nullptr,
      buf.memory, kHandleType};
  int fd = -1;
  if (vkGetMemoryFdKHR == nullptr)
  {
    throw std::runtime_error(
        "[OIDNDenoisePass] vkGetMemoryFdKHR is NULL — "
        "VK_KHR_external_memory_fd extension not loaded.");
  }
  NVVK_CHECK(vkGetMemoryFdKHR(device, &getFdInfo, &fd));
  buf.oidnBuf = m_oidnDevice.newBuffer(kOIDNHandleType, fd, byteSize);
#endif

  return buf;
}

/**********************************************************/
OIDNDenoisePass::ExternalBuffer
OIDNDenoisePass::allocateHostBuffer(size_t byteSize, VkBufferUsageFlags usage)
/**********************************************************/
{
  ExternalBuffer buf;
  buf.byteSize = byteSize;

  VkDevice         device     = m_contextManager->getDevice();
  VkPhysicalDevice physDevice = m_contextManager->getPhysicalDevice();

  VkBufferCreateInfo bufCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufCI.size        = byteSize;
  bufCI.usage       = usage;
  bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  NVVK_CHECK(vkCreateBuffer(device, &bufCI, nullptr, &buf.buffer));

  VkMemoryRequirements memReqs;
  vkGetBufferMemoryRequirements(device, buf.buffer, &memReqs);

  const uint32_t memTypeIdx = findMemoryType(
      physDevice, memReqs.memoryTypeBits,
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
  allocInfo.allocationSize  = memReqs.size;
  allocInfo.memoryTypeIndex = memTypeIdx;

  NVVK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &buf.memory));
  NVVK_CHECK(vkBindBufferMemory(device, buf.buffer, buf.memory, 0));
  NVVK_CHECK(vkMapMemory(device, buf.memory, 0, byteSize, 0, &buf.hostPtr));

  buf.oidnBuf = m_oidnDevice.newBuffer(buf.hostPtr, byteSize);
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
