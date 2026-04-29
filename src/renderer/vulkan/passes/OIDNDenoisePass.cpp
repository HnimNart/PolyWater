#include "OIDNDenoisePass.hpp"

#include <cstring>

#include "backend/vulkan/core/RenderContext.hpp"
#include "nvvk/check_error.hpp"
#include "nvvk/debug_util.hpp"
#include "nvvk/gbuffers.hpp"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

// Find a Vulkan memory type that satisfies the given type-filter bits and
// property flags.  Returns UINT32_MAX if none is found.
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

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

/**********************************************************/
OIDNDenoisePass::OIDNDenoisePass(VulkanContextManager* contextManager) :
    m_contextManager(contextManager)
/**********************************************************/
{
}

/**********************************************************/
OIDNDenoisePass::~OIDNDenoisePass()
/**********************************************************/
{
  // deinit() should have been called; guard against accidental double-free.
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**********************************************************/
void OIDNDenoisePass::init()
/**********************************************************/
{
  // --- Fence for the intermediate command-buffer submission ---
  VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  NVVK_CHECK(vkCreateFence(m_contextManager->getDevice(), &fenceInfo, nullptr,
                           &m_fence));

  createOIDNDevice();
  // Buffers are lazily created in execute() once the GBuffer size is known.
}

/**********************************************************/
void OIDNDenoisePass::deinit()
/**********************************************************/
{
  m_filter = oidn::FilterRef{};  // Release before device
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
void OIDNDenoisePass::execute(IRenderContext& ctx)
/**********************************************************/
{
  VulkanRenderContext& vkCtx = VulkanRenderContext::get(ctx);
  VkCommandBuffer cmd = vkCtx.cmdBuffer;
  const nvvk::GBuffer* gBuffers = vkCtx.gBuffers;
  const VkExtent2D size = gBuffers->getSize();

  NVVK_DBG_SCOPE(cmd);

  // -----------------------------------------------------------------------
  // 1. (Re-)create buffers when the resolution changes.
  // -----------------------------------------------------------------------
  if (size.width != m_width || size.height != m_height)
  {
    m_contextManager->waitForDeviceIdle();
    destroyBuffers();
    createBuffers(size.width, size.height);
    m_width = size.width;
    m_height = size.height;
  }

  // If buffer creation failed (no suitable memory), skip denoising gracefully.
  if (m_colorBuf.buffer == VK_NULL_HANDLE)
    return;

  // -----------------------------------------------------------------------
  // 2. Copy GBuffer images → OIDN input VkBuffers.
  //    (The render graph already transitioned them to TRANSFER_SRC_OPTIMAL.)
  // -----------------------------------------------------------------------
  auto copyImageToBuffer = [&](RenderOutput src, VkBuffer dst)
  {
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;  // Tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {size.width, size.height, 1};
    vkCmdCopyImageToBuffer(cmd, gBuffers->getColorImage(src),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, 1,
                           &region);
  };

  copyImageToBuffer(RenderOutput::Linear, m_colorBuf.buffer);
  copyImageToBuffer(RenderOutput::Albedo, m_albedoBuf.buffer);
  copyImageToBuffer(RenderOutput::Normal, m_normalBuf.buffer);

  // -----------------------------------------------------------------------
  // 3. Submit the current command buffer and wait on the CPU.
  //    This guarantees all Vulkan writes to the input buffers are visible.
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
  NVVK_CHECK(vkWaitForFences(m_contextManager->getDevice(), 1, &m_fence,
                             VK_TRUE, UINT64_MAX));

  // -----------------------------------------------------------------------
  // 4. Execute the OIDN filter on the GPU device.
  //    oidnDevice.sync() ensures the CUDA/HIP work is fully complete before
  //    we hand the output buffer back to Vulkan.
  // -----------------------------------------------------------------------
  m_filter.execute();

  // Use the OIDN C API value directly to avoid conflict with X11's None macro.
  const char* errorMessage = nullptr;
  if (m_oidnDevice.getError(errorMessage) != oidn::Error{OIDN_ERROR_NONE})
  {
    // Non-fatal: log and carry on – the output buffer may contain garbage.
    fprintf(stderr, "[OIDNDenoisePass] OIDN error: %s\n", errorMessage);
  }

  m_oidnDevice.sync();

  // -----------------------------------------------------------------------
  // 5. Allocate a fresh command buffer from the same pool and record the
  //    copy from the OIDN output buffer into the Denoised G-buffer image.
  //    A memory barrier ensures Vulkan sees the CUDA writes.
  // -----------------------------------------------------------------------
  VkCommandBufferAllocateInfo allocInfo{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocInfo.commandPool = vkCtx.cmdPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer newCmd = VK_NULL_HANDLE;
  NVVK_CHECK(vkAllocateCommandBuffers(m_contextManager->getDevice(), &allocInfo,
                                      &newCmd));

  VkCommandBufferBeginInfo beginInfo{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  NVVK_CHECK(vkBeginCommandBuffer(newCmd, &beginInfo));

  // A memory barrier to make the OIDN (CUDA/HIP) writes visible to the
  // Vulkan TRANSFER stage.
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

  // Copy OIDN output buffer → Denoised image.
  // (The render graph already transitioned Denoised to TRANSFER_DST_OPTIMAL.)
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
    vkCmdCopyBufferToImage(newCmd, m_outputBuf.buffer,
                           gBuffers->getColorImage(RenderOutput::Denoised),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  }

  // Hand the new command buffer to the context so subsequent passes record
  // into it, and the FrameSynchronizationManager submits it at end-of-frame.
  vkCtx.cmdBuffer = newCmd;
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

  if (m_gpuPath)
  {
    m_colorBuf = allocateExternalBuffer(byteSize, kInputUsage);
    m_albedoBuf = allocateExternalBuffer(byteSize, kInputUsage);
    m_normalBuf = allocateExternalBuffer(byteSize, kInputUsage);
    m_outputBuf = allocateExternalBuffer(byteSize, kOutputUsage);
    // allocateExternalBuffer may fall back to host buffers internally if
    // external memory is unavailable; both paths set a valid oidnBuf.
  }
  else
  {
    m_colorBuf = allocateHostBuffer(byteSize, kInputUsage);
    m_albedoBuf = allocateHostBuffer(byteSize, kInputUsage);
    m_normalBuf = allocateHostBuffer(byteSize, kInputUsage);
    m_outputBuf = allocateHostBuffer(byteSize, kOutputUsage);
  }

  // Bail out if any allocation failed (e.g., no host-visible memory type).
  if (m_colorBuf.buffer == VK_NULL_HANDLE ||
      m_albedoBuf.buffer == VK_NULL_HANDLE ||
      m_normalBuf.buffer == VK_NULL_HANDLE ||
      m_outputBuf.buffer == VK_NULL_HANDLE)
  {
    fprintf(
        stderr,
        "[OIDNDenoisePass] Buffer allocation failed; denoising disabled.\n");
    destroyBuffers();
    return;
  }

  rebuildFilter(width, height);
}

/**********************************************************/
void OIDNDenoisePass::destroyBuffers()
/**********************************************************/
{
  m_filter =
      oidn::FilterRef{};  // Must be released before the buffers it references

  destroyBuffer(m_colorBuf);
  destroyBuffer(m_albedoBuf);
  destroyBuffer(m_normalBuf);
  destroyBuffer(m_outputBuf);
  m_width = 0;
  m_height = 0;
}

/**********************************************************/
void OIDNDenoisePass::rebuildFilter(uint32_t width, uint32_t height)
/**********************************************************/
{
  m_filter = m_oidnDevice.newFilter("RT");

  // Tell OIDN about the Float4 pixel layout (skip the alpha channel).
  const size_t byteRowStride = static_cast<size_t>(width) * kBytesPerPixel;
  const size_t bytePixelStride = kBytesPerPixel;

  m_filter.setImage("color", m_colorBuf.oidnBuf, oidn::Format::Float3, width,
                    height, 0, byteRowStride, bytePixelStride);
  m_filter.setImage("albedo", m_albedoBuf.oidnBuf, oidn::Format::Float3, width,
                    height, 0, byteRowStride, bytePixelStride);
  m_filter.setImage("normal", m_normalBuf.oidnBuf, oidn::Format::Float3, width,
                    height, 0, byteRowStride, bytePixelStride);
  m_filter.setImage("output", m_outputBuf.oidnBuf, oidn::Format::Float3, width,
                    height, 0, byteRowStride, bytePixelStride);

  m_filter.set("hdr", true);  // Input is HDR (linear)
  m_filter.commit();
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
