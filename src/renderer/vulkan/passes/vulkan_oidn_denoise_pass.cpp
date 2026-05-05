#include "vulkan_oidn_denoise_pass.hpp"

#include <cstring>
#include <stdexcept>

#include "backend/vulkan/core/vulkan_render_context.hpp"
#include "nvvk/check_error.hpp"
#include "nvvk/gbuffers.hpp"

// ---------------------------------------------------------------------------
// Helpers
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

constexpr VkBufferUsageFlags kInputUsage =
    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
constexpr VkBufferUsageFlags kOutputUsage =
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

}  // namespace

/**********************************************************/
OIDNDenoisePass::OIDNDenoisePass(VulkanContextManager* contextManager) :
    m_contextManager(contextManager)
/**********************************************************/
{
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**********************************************************/
void OIDNDenoisePass::init()
/**********************************************************/
{
  createOIDNDevice();

  // Create the timeline semaphore that the OIDN worker signals from the CPU
  // after each inference.  CB3's GPU submission waits on this value so the
  // vkCmdCopyBufferToImage only executes after OIDN has written valid data.
  const VkSemaphoreTypeCreateInfo timelineInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue = 0,
  };
  const VkSemaphoreCreateInfo semCI{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &timelineInfo,
  };
  NVVK_CHECK(vkCreateSemaphore(m_contextManager->getDevice(), &semCI, nullptr,
                               &m_oidnTimelineSemaphore));

  // Start the persistent OIDN worker thread.
  m_oidnThread = std::thread([this]() { oidnWorkerLoop(); });
}

/**********************************************************/
void OIDNDenoisePass::deinit()
/**********************************************************/
{
  // Signal the worker to stop and drain its queue before joining.
  {
    std::lock_guard<std::mutex> lock(m_workerMutex);
    m_workerStop = true;
  }
  m_workerCv.notify_one();
  if (m_oidnThread.joinable())
  {
    m_oidnThread.join();
  }

  // Worker has exited — all vkSignalSemaphore calls have been made.
  destroyAllSlots();
  m_oidnDevice = oidn::DeviceRef{};

  if (m_oidnTimelineSemaphore != VK_NULL_HANDLE)
  {
    vkDestroySemaphore(m_contextManager->getDevice(), m_oidnTimelineSemaphore,
                       nullptr);
    m_oidnTimelineSemaphore = VK_NULL_HANDLE;
  }
}

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

// ---------------------------------------------------------------------------
// copyBufferToDenoised helper
// ---------------------------------------------------------------------------

/**********************************************************/
void OIDNDenoisePass::copyBufferToDenoised(VkCommandBuffer cmd,
                                           const nvvk::GBuffer* gBuffers,
                                           VkExtent2D size,
                                           VkBuffer outputBuffer)
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

  vkCmdCopyBufferToImage(cmd, outputBuffer,
                         gBuffers->getColorImage(RenderOutput::Denoised),
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

// ---------------------------------------------------------------------------
// execute()  — called once per frame; never blocks the CPU for OIDN
// ---------------------------------------------------------------------------

/**********************************************************/
void OIDNDenoisePass::execute(IRenderContext& ctx)
/**********************************************************/
{
  VulkanRenderContext& vkCtx = VulkanRenderContext::get(ctx);
  const uint32_t slot = vkCtx.frameRingIndex;

  VkCommandBuffer cmd = vkCtx.cmdBuffer;
  const nvvk::GBuffer* gBuffers = vkCtx.gBuffers;
  const VkExtent2D size = gBuffers->getSize();

  // -----------------------------------------------------------------------
  // Recreate all slots when the resolution changes.
  //
  // waitForDeviceIdle() drains all pending GPU work including CB3 submissions
  // that are waiting on m_oidnTimelineSemaphore.  Because the OIDN worker runs
  // on a separate thread it will signal those values independently, allowing
  // the GPU to complete and waitForDeviceIdle to return.
  // -----------------------------------------------------------------------
  if (size.width != m_width || size.height != m_height)
  {
    m_contextManager->waitForDeviceIdle();
    destroyAllSlots();
    m_width = size.width;
    m_height = size.height;
  }

  // Lazily create the slot for this ring index on first use (or after resize).
  ensureSlot(slot, size.width, size.height);

  auto& fd = m_frameData[slot];
  if (fd.colorBuf.buffer == VK_NULL_HANDLE)
  {
    return;  // Buffer allocation failed; skip denoising this frame.
  }

  // -----------------------------------------------------------------------
  // 1. Record GBuffer → OIDN input buffer copies into CB1.
  // -----------------------------------------------------------------------
  auto copyImageToBuffer = [&](RenderOutput src, VkBuffer dst)
  {
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {size.width, size.height, 1};

    vkCmdCopyImageToBuffer(cmd, gBuffers->getColorImage(src),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, 1,
                           &region);
  };

  copyImageToBuffer(RenderOutput::Linear, fd.colorBuf.buffer);
  copyImageToBuffer(RenderOutput::Albedo, fd.albedoBuf.buffer);
  copyImageToBuffer(RenderOutput::Normal, fd.normalBuf.buffer);

  // -----------------------------------------------------------------------
  // 2. End CB1 and submit it with the per-slot fence.
  //    The OIDN worker will wait on this fence before reading the buffers.
  //    The main thread does NOT wait.
  // -----------------------------------------------------------------------
  NVVK_CHECK(vkEndCommandBuffer(cmd));
  NVVK_CHECK(vkResetFences(m_contextManager->getDevice(), 1, &fd.fence));

  const VkCommandBufferSubmitInfo cb1Info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = cmd,
  };
  const VkSubmitInfo2 cb1Submit{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = &cb1Info,
  };
  NVVK_CHECK(vkQueueSubmit2(m_contextManager->getQueueInfo(0).queue, 1,
                            &cb1Submit, fd.fence));

  // -----------------------------------------------------------------------
  // 3. Push a job onto the OIDN worker queue.  The worker will:
  //      a. vkWaitForFences(fd.fence)  — GPU copy done, buffers ready
  //      b. filter.executeAsync() + sync()  — OIDN inference
  //      c. vkSignalSemaphore(m_oidnTimelineSemaphore, signalValue)
  //    Step (c) is a CPU-to-GPU signal: it unblocks CB3 on the GPU without
  //    any involvement from the main render thread.
  // -----------------------------------------------------------------------
  const uint64_t signalValue = ++m_oidnSignalCounter;
  {
    auto job = std::make_unique<WorkerJob>();
    job->fence = fd.fence;
    job->device = m_contextManager->getDevice();
    job->filter = fd.filter;
    job->oidnDevice = m_oidnDevice;
    job->semaphore = m_oidnTimelineSemaphore;
    job->signalValue = signalValue;

    std::lock_guard<std::mutex> lock(m_workerMutex);
    m_jobQueue.push(std::move(job));
  }
  m_workerCv.notify_one();

  // Store the semaphore and value in the context so VulkanBackend::endFrame()
  // can inject them as a GPU-side wait on CB3's VkQueueSubmit2 call.
  vkCtx.oidnSemaphore = m_oidnTimelineSemaphore;
  vkCtx.oidnWaitValue = signalValue;

  // -----------------------------------------------------------------------
  // 4. Prepare CB3 for post-OIDN passes (ToneMap, UI).
  //    Reuse the pre-allocated command buffer for this slot; it was reset by
  //    vkResetCommandPool in VulkanFrameSynchronizationManager::beginFrame().
  // -----------------------------------------------------------------------
  if (fd.cb3 == VK_NULL_HANDLE)
  {
    // First use for this slot: allocate once from the frame's command pool.
    const VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vkCtx.cmdPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    NVVK_CHECK(vkAllocateCommandBuffers(m_contextManager->getDevice(),
                                        &allocInfo, &fd.cb3));
    fd.cb3Pool = vkCtx.cmdPool;
  }

  const VkCommandBufferBeginInfo beginInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  NVVK_CHECK(vkBeginCommandBuffer(fd.cb3, &beginInfo));

  // Memory barrier: the timeline semaphore wait (added by endFrame) provides
  // execution ordering, but we still need an access-scope barrier so the GPU
  // copy sees the OIDN output bytes written by the CPU (host-visible path) or
  // CUDA (GPU path) as a coherent transfer-read.
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
    vkCmdPipelineBarrier2(fd.cb3, &depInfo);
  }

  copyBufferToDenoised(fd.cb3, gBuffers, size, fd.outputBuf.buffer);

  // Hand CB3 off to subsequent passes (ToneMap, UI).
  vkCtx.cmdBuffer = fd.cb3;
}

// ---------------------------------------------------------------------------
// Async OIDN worker thread
// ---------------------------------------------------------------------------

/**********************************************************/
void OIDNDenoisePass::oidnWorkerLoop()
/**********************************************************/
{
  // Run until deinit() sets m_workerStop and notifies.  Drain the job queue
  // completely before exiting so all vkSignalSemaphore calls are made.
  while (true)
  {
    std::unique_ptr<WorkerJob> job;
    {
      std::unique_lock<std::mutex> lock(m_workerMutex);
      m_workerCv.wait(lock,
                      [this]() { return m_workerStop || !m_jobQueue.empty(); });
      if (m_workerStop && m_jobQueue.empty())
        break;

      job = std::move(m_jobQueue.front());
      m_jobQueue.pop();
    }

    // Wait for CB1 to complete on the GPU (OIDN input buffers are ready).
    vkWaitForFences(job->device, 1, &job->fence, VK_TRUE, UINT64_MAX);

    // Run OIDN inference (CPU path or GPU backend via CUDA/HIP/SYCL).
    job->filter.executeAsync();
    job->oidnDevice.sync();

    // Log OIDN errors (non-fatal; denoising quality degrades, not correctness).
    const char* errorMessage = nullptr;
    if (job->oidnDevice.getError(errorMessage) != oidn::Error{OIDN_ERROR_NONE})
    {
      fprintf(stderr, "[OIDNDenoisePass] OIDN error: %s\n", errorMessage);
    }

    // Signal the Vulkan timeline semaphore from the CPU.
    // This unblocks CB3 on the GPU — no main-thread involvement required.
    const VkSemaphoreSignalInfo signalInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
        .semaphore = job->semaphore,
        .value = job->signalValue,
    };
    vkSignalSemaphore(job->device, &signalInfo);
  }
}

// ---------------------------------------------------------------------------
// OIDN device creation
// ---------------------------------------------------------------------------

/**********************************************************/
void OIDNDenoisePass::createOIDNDevice()
/**********************************************************/
{
  // auto-select the best available device (GPU first, CPU fallback).
  m_oidnDevice = oidn::newDevice();
  m_oidnDevice.commit();

  // Determine whether we can use Vulkan external-memory sharing.
  // External memory is supported by GPU backends (CUDA, HIP, SYCL).
  int devType = m_oidnDevice.get<int>("type");
  m_gpuPath = (devType == (int) oidn::DeviceType::CUDA ||
               devType == (int) oidn::DeviceType::HIP ||
               devType == (int) oidn::DeviceType::SYCL);
}

// ---------------------------------------------------------------------------
// Per-slot lifecycle
// ---------------------------------------------------------------------------

/**********************************************************/
void OIDNDenoisePass::ensureSlot(uint32_t idx, uint32_t width, uint32_t height)
/**********************************************************/
{
  if (idx >= m_frameData.size())
  {
    m_frameData.resize(idx + 1);
  }

  auto& fd = m_frameData[idx];
  if (fd.colorBuf.buffer != VK_NULL_HANDLE)
  {
    return;  // Already allocated for this resolution.
  }

  const size_t byteSize =
      static_cast<size_t>(width) * height * kBytesPerPixel;

  if (m_gpuPath)
  {
    fd.colorBuf = allocateExternalBuffer(byteSize, kInputUsage);
    fd.albedoBuf = allocateExternalBuffer(byteSize, kInputUsage);
    fd.normalBuf = allocateExternalBuffer(byteSize, kInputUsage);
    fd.outputBuf = allocateExternalBuffer(byteSize, kOutputUsage);
  }
  else
  {
    fd.colorBuf = allocateHostBuffer(byteSize, kInputUsage);
    fd.albedoBuf = allocateHostBuffer(byteSize, kInputUsage);
    fd.normalBuf = allocateHostBuffer(byteSize, kInputUsage);
    fd.outputBuf = allocateHostBuffer(byteSize, kOutputUsage);
  }

  if (fd.colorBuf.buffer == VK_NULL_HANDLE)
  {
    fprintf(stderr,
            "[OIDNDenoisePass] Buffer allocation failed for slot %u; "
            "denoising disabled.\n",
            idx);
    return;
  }

  rebuildFilter(idx, width, height);

  // Per-slot fence starts signaled so the very first vkResetFences call
  // succeeds without requiring a prior vkWaitForFences.
  const VkFenceCreateInfo fenceInfo{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT,
  };
  NVVK_CHECK(vkCreateFence(m_contextManager->getDevice(), &fenceInfo, nullptr,
                           &fd.fence));
  // cb3 is allocated lazily on first execute() for this slot.
}

/**********************************************************/
void OIDNDenoisePass::destroySlot(FrameData& fd)
/**********************************************************/
{
  fd.filter = oidn::FilterRef{};  // Release OIDN filter before its buffers.
  destroyBuffer(fd.colorBuf);
  destroyBuffer(fd.albedoBuf);
  destroyBuffer(fd.normalBuf);
  destroyBuffer(fd.outputBuf);

  VkDevice device = m_contextManager->getDevice();

  if (fd.cb3 != VK_NULL_HANDLE && fd.cb3Pool != VK_NULL_HANDLE)
  {
    vkFreeCommandBuffers(device, fd.cb3Pool, 1, &fd.cb3);
    fd.cb3 = VK_NULL_HANDLE;
    fd.cb3Pool = VK_NULL_HANDLE;
  }

  if (fd.fence != VK_NULL_HANDLE)
  {
    vkDestroyFence(device, fd.fence, nullptr);
    fd.fence = VK_NULL_HANDLE;
  }
}

/**********************************************************/
void OIDNDenoisePass::destroyAllSlots()
/**********************************************************/
{
  // Ensure all in-flight GPU work — including CB3 submissions blocked on the
  // OIDN timeline semaphore — has completed before releasing resources.
  // The OIDN worker (still running during resolution changes) will signal
  // the semaphore values so the GPU can proceed and waitForDeviceIdle returns.
  if (!m_frameData.empty())
  {
    m_contextManager->waitForDeviceIdle();
  }

  for (auto& fd : m_frameData)
  {
    destroySlot(fd);
  }
  m_frameData.clear();
}

/**********************************************************/
void OIDNDenoisePass::rebuildFilter(uint32_t slot, uint32_t width,
                                    uint32_t height)
/**********************************************************/
{
  auto& fd = m_frameData[slot];
  fd.filter = m_oidnDevice.newFilter("RT");

  // Assuming VK_FORMAT_R32G32B32A32_SFLOAT (16 bytes per pixel).
  const size_t bytePixelStride = 16;
  const size_t byteRowStride = static_cast<size_t>(width) * bytePixelStride;

  fd.filter.setImage("color", fd.colorBuf.oidnBuf, oidn::Format::Float3, width,
                     height, 0, bytePixelStride, byteRowStride);
  fd.filter.setImage("albedo", fd.albedoBuf.oidnBuf, oidn::Format::Float3,
                     width, height, 0, bytePixelStride, byteRowStride);
  fd.filter.setImage("normal", fd.normalBuf.oidnBuf, oidn::Format::Float3,
                     width, height, 0, bytePixelStride, byteRowStride);
  fd.filter.setImage("output", fd.outputBuf.oidnBuf, oidn::Format::Float3,
                     width, height, 0, bytePixelStride, byteRowStride);

  fd.filter.set("hdr", true);
  // Balanced quality gives ~2× throughput over Default with good visual output.
  fd.filter.set("quality", static_cast<int>(OIDN_QUALITY_BALANCED));
  fd.filter.commit();
}

// ---------------------------------------------------------------------------
// Buffer management (host-visible fallback + external-memory GPU path)
// ---------------------------------------------------------------------------
/**********************************************************/
OIDNDenoisePass::ExternalBuffer
OIDNDenoisePass::allocateExternalBuffer(size_t byteSize,
                                        VkBufferUsageFlags usage)
/**********************************************************/
{
  ExternalBuffer buf;
  buf.byteSize = byteSize;

  VkDevice device = m_contextManager->getDevice();
  VkPhysicalDevice physDevice = m_contextManager->getPhysicalDevice();

  // ---- Select platform external-memory handle type ----
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

  // ---- Verify external memory is actually exportable ----
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
    // Fall back to host-visible path
    return allocateHostBuffer(byteSize, usage);
  }

  // ---- Create VkBuffer with external-memory flag ----
  VkExternalMemoryBufferCreateInfo extBufInfo{
      VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
  extBufInfo.handleTypes = kHandleType;

  VkBufferCreateInfo bufCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufCI.pNext = &extBufInfo;
  bufCI.size = byteSize;
  bufCI.usage = usage;
  bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  NVVK_CHECK(vkCreateBuffer(device, &bufCI, nullptr, &buf.buffer));

  // ---- Allocate device-local memory with export capability ----
  VkMemoryRequirements memReqs;
  vkGetBufferMemoryRequirements(device, buf.buffer, &memReqs);

  uint32_t memTypeIdx = findMemoryType(physDevice, memReqs.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (memTypeIdx == UINT32_MAX)
  {
    // Unusual: no device-local exportable memory – fall back.
    vkDestroyBuffer(device, buf.buffer, nullptr);
    buf.buffer = VK_NULL_HANDLE;
    return allocateHostBuffer(byteSize, usage);
  }

  VkExportMemoryAllocateInfo exportInfo{
      VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
  exportInfo.handleTypes = kHandleType;

  VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocInfo.pNext = &exportInfo;
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = memTypeIdx;

  NVVK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &buf.memory));
  NVVK_CHECK(vkBindBufferMemory(device, buf.buffer, buf.memory, 0));

  // ---- Export the memory handle and import into OIDN ----
#ifdef _WIN32
  VkMemoryGetWin32HandleInfoKHR getHandleInfo{
      VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR};
  getHandleInfo.memory = buf.memory;
  getHandleInfo.handleType = kHandleType;
  HANDLE win32Handle = nullptr;
  NVVK_CHECK(vkGetMemoryWin32HandleKHR(device, &getHandleInfo, &win32Handle));
  buf.oidnBuf =
      m_oidnDevice.newBuffer(kOIDNHandleType, win32Handle, nullptr, byteSize);
#else
  VkMemoryGetFdInfoKHR getFdInfo{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
  getFdInfo.memory = buf.memory;
  getFdInfo.handleType = kHandleType;
  int fd = -1;
  if (vkGetMemoryFdKHR == nullptr)
  {
    throw std::runtime_error(
        "vkGetMemoryFdKHR is NULL! Extension not enabled or loaded.");
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

  VkDevice device = m_contextManager->getDevice();
  VkPhysicalDevice physDevice = m_contextManager->getPhysicalDevice();

  VkBufferCreateInfo bufCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufCI.size = byteSize;
  bufCI.usage = usage;
  bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  NVVK_CHECK(vkCreateBuffer(device, &bufCI, nullptr, &buf.buffer));

  VkMemoryRequirements memReqs;
  vkGetBufferMemoryRequirements(device, buf.buffer, &memReqs);

  uint32_t memTypeIdx =
      findMemoryType(physDevice, memReqs.memoryTypeBits,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (memTypeIdx == UINT32_MAX)
  {
    // This should never happen on any real GPU; all Vulkan-capable hardware
    // must expose at least one host-visible heap.
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

  // OIDN shared buffer backed by the host-mapped pointer.
  buf.oidnBuf = m_oidnDevice.newBuffer(buf.hostPtr, byteSize);

  return buf;
}

/**********************************************************/
void OIDNDenoisePass::destroyBuffer(ExternalBuffer& buf)
/**********************************************************/
{
  buf.oidnBuf = oidn::BufferRef{};  // Release OIDN side first

  VkDevice device = m_contextManager->getDevice();
  if (buf.hostPtr)
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
