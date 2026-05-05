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
  VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  NVVK_CHECK(vkCreateFence(m_contextManager->getDevice(), &fenceInfo, nullptr,
                           &m_fence));

  createOIDNDevice();

  // Start the persistent OIDN worker thread.
  m_oidnThread = std::thread([this]() { oidnWorkerLoop(); });
}

/**********************************************************/
void OIDNDenoisePass::deinit()
/**********************************************************/
{
  // Signal and join the worker thread before releasing OIDN resources.
  {
    std::lock_guard<std::mutex> lock(m_workerMutex);
    m_workerStop = true;
  }
  m_workerCv.notify_one();
  if (m_oidnThread.joinable())
  {
    m_oidnThread.join();
  }

  // Worker has exited; any pending future is already fulfilled.
  destroyBuffers();
  m_oidnDevice = oidn::DeviceRef{};  // Release OIDN device

  if (m_fence != VK_NULL_HANDLE)
  {
    vkDestroyFence(m_contextManager->getDevice(), m_fence, nullptr);
    m_fence = VK_NULL_HANDLE;
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
// execute()  – the main entry point, called once per frame
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

/**********************************************************/
void OIDNDenoisePass::execute(IRenderContext& ctx)
/**********************************************************/
{
  VulkanRenderContext& vkCtx = VulkanRenderContext::get(ctx);

  VkCommandBuffer cmd = vkCtx.cmdBuffer;
  const nvvk::GBuffer* gBuffers = vkCtx.gBuffers;
  const VkExtent2D size = gBuffers->getSize();

  // re-create buffers when the resolution changes.
  if (size.width != m_width || size.height != m_height)
  {
    m_contextManager->waitForDeviceIdle();
    destroyBuffers();  // drains m_prevOidnFuture before releasing memory
    createBuffers(size.width, size.height);
    m_width = size.width;
    m_height = size.height;
  }

  // If buffer creation failed, skip denoising.
  if (m_frameData[0].colorBuf.buffer == VK_NULL_HANDLE)
  {
    return;
  }

  const int curr = m_pingPong;
  const int prev = 1 - curr;
  const bool isFirst = m_firstFrame;

  // -----------------------------------------------------------------------
  // 1. Wait for the previous frame's OIDN job.
  //    Because the worker has been running OIDN while this frame's GPU render
  //    pass executed, this is typically a no-op (or very short) wait.
  //    On the first frame m_prevOidnFuture is not yet valid, so this is
  //    skipped entirely.
  // -----------------------------------------------------------------------
  if (m_prevOidnFuture.valid())
  {
    m_prevOidnFuture.get();
  }

  // -----------------------------------------------------------------------
  // 2. Record GBuffer → OIDN input buffer copies into the current command
  //    buffer (slot curr).
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

  copyImageToBuffer(RenderOutput::Linear, m_frameData[curr].colorBuf.buffer);
  copyImageToBuffer(RenderOutput::Albedo, m_frameData[curr].albedoBuf.buffer);
  copyImageToBuffer(RenderOutput::Normal, m_frameData[curr].normalBuf.buffer);

  // -----------------------------------------------------------------------
  // 3. Submit the command buffer so the GPU DMA-s image data into the OIDN
  //    input buffers.  Signal the fence so the worker can wait on it.
  // -----------------------------------------------------------------------
  NVVK_CHECK(vkEndCommandBuffer(cmd));

  VkCommandBufferSubmitInfo cmdSubmitInfo{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  cmdSubmitInfo.commandBuffer = cmd;

  VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  submitInfo.commandBufferInfoCount = 1;
  submitInfo.pCommandBufferInfos = &cmdSubmitInfo;

  NVVK_CHECK(vkResetFences(m_contextManager->getDevice(), 1, &m_fence));
  NVVK_CHECK(vkQueueSubmit2(m_contextManager->getQueueInfo(0).queue, 1,
                            &submitInfo, m_fence));

  // -----------------------------------------------------------------------
  // 4. Post the OIDN job for slot curr to the worker thread.
  //    The main render thread does NOT wait — it proceeds immediately.
  //    The worker will call vkWaitForFences → executeAsync() → sync().
  // -----------------------------------------------------------------------
  {
    auto job = std::make_unique<WorkerJob>();
    job->fence = m_fence;
    job->device = m_contextManager->getDevice();
    job->filter = m_frameData[curr].filter;
    job->oidnDevice = m_oidnDevice;
    m_prevOidnFuture = job->completion.get_future();

    std::lock_guard<std::mutex> lock(m_workerMutex);
    m_pendingJob = std::move(job);
  }
  m_workerCv.notify_one();

  // On the very first frame, block once so we have a valid denoised image to
  // display immediately rather than showing an uninitialised buffer.
  if (isFirst)
  {
    m_prevOidnFuture.get();
    m_prevOidnFuture = {};  // Mark as consumed; next frame skips the wait
    m_firstFrame = false;
    m_prevSlotHasOutput = true;
  }

  // -----------------------------------------------------------------------
  // 5. Allocate a fresh command buffer for subsequent passes (ToneMap, UI).
  //    Copy the *previous* frame's denoised output into the Denoised GBuffer.
  //    (On the first frame, "previous" == current because we blocked above.)
  // -----------------------------------------------------------------------
  VkCommandBufferAllocateInfo allocInfo{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocInfo.commandPool = vkCtx.cmdPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer newCmd = VK_NULL_HANDLE;
  NVVK_CHECK(vkAllocateCommandBuffers(m_contextManager->getDevice(), &allocInfo,
                                      &newCmd));

  VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  NVVK_CHECK(vkBeginCommandBuffer(newCmd, &beginInfo));

  {
    VkMemoryBarrier2 memBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    memBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;

    VkDependencyInfo depInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.memoryBarrierCount = 1;
    depInfo.pMemoryBarriers = &memBarrier;
    vkCmdPipelineBarrier2(newCmd, &depInfo);
  }

  if (m_prevSlotHasOutput)
  {
    // First frame:      display curr (we blocked; OIDN is complete).
    // Subsequent frames: display prev (OIDN finished while this frame rendered).
    const int displaySlot = isFirst ? curr : prev;
    copyBufferToDenoised(newCmd, gBuffers, size,
                         m_frameData[displaySlot].outputBuf.buffer);
  }

  vkCtx.cmdBuffer = newCmd;

  // Advance to the next ping-pong slot.
  m_pingPong = 1 - m_pingPong;
}

// ---------------------------------------------------------------------------
// Async worker thread
// ---------------------------------------------------------------------------

/**********************************************************/
void OIDNDenoisePass::oidnWorkerLoop()
/**********************************************************/
{
  while (true)
  {
    std::unique_ptr<WorkerJob> job;
    {
      std::unique_lock<std::mutex> lock(m_workerMutex);
      m_workerCv.wait(lock,
                      [this]() { return m_pendingJob || m_workerStop; });
      if (m_workerStop)
      {
        break;
      }
      job = std::move(m_pendingJob);
    }

    // Wait for the GPU to finish DMA-ing image data into the OIDN buffers.
    VkResult fenceResult =
        vkWaitForFences(job->device, 1, &job->fence, VK_TRUE, UINT64_MAX);
    if (fenceResult != VK_SUCCESS)
    {
      job->completion.set_exception(std::make_exception_ptr(
          std::runtime_error("[OIDNDenoisePass] vkWaitForFences failed")));
      continue;
    }

    // Run the filter for this slot asynchronously, then sync.
    job->filter.executeAsync();
    job->oidnDevice.sync();

    // Log any OIDN error (non-fatal).
    const char* errorMessage = nullptr;
    if (job->oidnDevice.getError(errorMessage) != oidn::Error{OIDN_ERROR_NONE})
    {
      fprintf(stderr, "[OIDNDenoisePass] OIDN error: %s\n", errorMessage);
    }

    // Unblock whoever is waiting on this future (if anyone).
    job->completion.set_value();
  }
}

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
// Buffer management
// ---------------------------------------------------------------------------

/**********************************************************/
void OIDNDenoisePass::createBuffers(uint32_t width, uint32_t height)
/**********************************************************/
{
  const size_t byteSize = static_cast<size_t>(width) * height * kBytesPerPixel;

  constexpr VkBufferUsageFlags kInputUsage =
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  constexpr VkBufferUsageFlags kOutputUsage =
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

  for (int i = 0; i < 2; ++i)
  {
    if (m_gpuPath)
    {
      m_frameData[i].colorBuf = allocateExternalBuffer(byteSize, kInputUsage);
      m_frameData[i].albedoBuf = allocateExternalBuffer(byteSize, kInputUsage);
      m_frameData[i].normalBuf = allocateExternalBuffer(byteSize, kInputUsage);
      m_frameData[i].outputBuf = allocateExternalBuffer(byteSize, kOutputUsage);
    }
    else
    {
      m_frameData[i].colorBuf = allocateHostBuffer(byteSize, kInputUsage);
      m_frameData[i].albedoBuf = allocateHostBuffer(byteSize, kInputUsage);
      m_frameData[i].normalBuf = allocateHostBuffer(byteSize, kInputUsage);
      m_frameData[i].outputBuf = allocateHostBuffer(byteSize, kOutputUsage);
    }

    if (m_frameData[i].colorBuf.buffer == VK_NULL_HANDLE ||
        m_frameData[i].albedoBuf.buffer == VK_NULL_HANDLE ||
        m_frameData[i].normalBuf.buffer == VK_NULL_HANDLE ||
        m_frameData[i].outputBuf.buffer == VK_NULL_HANDLE)
    {
      fprintf(
          stderr,
          "[OIDNDenoisePass] Buffer allocation failed; denoising disabled.\n");
      destroyBuffers();
      return;
    }

    rebuildFilter(i, width, height);
  }
}

/**********************************************************/
void OIDNDenoisePass::destroyBuffers()
/**********************************************************/
{
  // Drain any pending OIDN job before releasing its buffers.
  if (m_prevOidnFuture.valid())
  {
    try
    {
      m_prevOidnFuture.get();
    }
    catch (...)
    {
      // Swallow errors during cleanup; we are tearing down anyway.
    }
  }

  for (int i = 0; i < 2; ++i)
  {
    m_frameData[i].filter = oidn::FilterRef{};  // Release filter before buffers
    destroyBuffer(m_frameData[i].colorBuf);
    destroyBuffer(m_frameData[i].albedoBuf);
    destroyBuffer(m_frameData[i].normalBuf);
    destroyBuffer(m_frameData[i].outputBuf);
  }

  m_width = 0;
  m_height = 0;
  m_pingPong = 0;
  m_firstFrame = true;
  m_prevSlotHasOutput = false;
}

/**********************************************************/
void OIDNDenoisePass::rebuildFilter(int slot, uint32_t width, uint32_t height)
/**********************************************************/
{
  auto& fd = m_frameData[slot];
  fd.filter = m_oidnDevice.newFilter("RT");

  // Assuming VK_FORMAT_R32G32B32A32_SFLOAT (16 bytes per pixel)
  const size_t bytePixelStride = 16;
  const size_t byteRowStride = static_cast<size_t>(width) * bytePixelStride;

  // Notice the order: byteOffset, pixelStride, rowStride
  fd.filter.setImage("color", fd.colorBuf.oidnBuf, oidn::Format::Float3, width,
                     height, 0, bytePixelStride, byteRowStride);

  fd.filter.setImage("albedo", fd.albedoBuf.oidnBuf, oidn::Format::Float3,
                     width, height, 0, bytePixelStride, byteRowStride);

  fd.filter.setImage("normal", fd.normalBuf.oidnBuf, oidn::Format::Float3,
                     width, height, 0, bytePixelStride, byteRowStride);

  fd.filter.setImage("output", fd.outputBuf.oidnBuf, oidn::Format::Float3,
                     width, height, 0, bytePixelStride, byteRowStride);

  fd.filter.set("hdr", true);
  // Use Balanced quality for a ~2x throughput improvement over Default while
  // maintaining good visual quality for real-time path tracing.
  fd.filter.set("quality", static_cast<int>(OIDN_QUALITY_BALANCED));
  fd.filter.commit();
}

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
