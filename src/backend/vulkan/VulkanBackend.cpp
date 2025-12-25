#include "VulkanBackend.hpp"

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_vulkan.h>
#include <volk.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/swapchain.hpp>

#include "VulkanRenderContext.hpp"
#include "VulkanSceneRenderer.hpp"
#include "backend/RenderContext.hpp"
#include "vk_utils.hpp"

namespace core
{

std::unique_ptr<VulkanBackend>
VulkanBackend::create(const core::ApplicationCreateInfo& appInfo,
                      const std::vector<std::filesystem::path>& shaderDirs)
{
  try
  {
    auto compiler = std::make_shared<SlangShaderCompiler>(shaderDirs);
    return std::unique_ptr<VulkanBackend>(new VulkanBackend(appInfo, compiler));
  }
  catch (const std::exception& e)
  {
    LOGE("Critical Error creating VulkanBackend: %s\n", e.what());
    return nullptr;
  }
}

VulkanBackend::VulkanBackend(const core::ApplicationCreateInfo& appInfo,
                             std::shared_ptr<SlangShaderCompiler> compiler) :
    m_compiler(std::move(compiler))
{
  nvvk::ContextInitInfo vkSetup = vk_utils::createVkContextInfo(appInfo);
  VkResult result = m_vkContext.init(vkSetup);
  if (result != VK_SUCCESS)
  {
    LOGE("Failed to initialize Vulkan context.\n");
    std::string errorMsg = "Vulkan Initialization Failed: " + std::string(string_VkResult(result));
    throw std::runtime_error(errorMsg);
  }
}

void VulkanBackend::init()
{
  // Validate window handle before surface creation
  if (!m_windowHandle)
  {
    throw std::runtime_error("VulkanBackend initialized without a valid GLFW window.");
  }

  VkDevice device = m_vkContext.getDevice();
  VkPhysicalDevice physicalDevice = m_vkContext.getPhysicalDevice();
  VkInstance instance = m_vkContext.getInstance();
  const nvvk::QueueInfo& graphicsQueue = m_vkContext.getQueueInfo(0);

  // 1. Create Window Surface
  NVVK_CHECK(glfwCreateWindowSurface(instance, m_windowHandle, nullptr, &m_surface));

  // 2. Create Transient Command Pool
  const VkCommandPoolCreateInfo cmdPoolInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
      .queueFamilyIndex = graphicsQueue.familyIndex,
  };
  NVVK_CHECK(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &m_transientCmdPool));
  NVVK_DBG_NAME(m_transientCmdPool);

  // 3. Create Descriptor Pool
  const std::array<VkDescriptorPoolSize, 1> poolSizes{{
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_maxTexturePool},
  }};

  const VkDescriptorPoolCreateInfo poolInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT |
               VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
      .maxSets = m_maxTexturePool,
      .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
      .pPoolSizes = poolSizes.data(),
  };
  NVVK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool));
  NVVK_DBG_NAME(m_descriptorPool);

  // 4. Create Swapchain
  nvvk::Swapchain::InitInfo swapChainInit{
      .physicalDevice = physicalDevice,
      .device = device,
      .surface = m_surface,
      .queue = graphicsQueue,
      .cmdPool = m_transientCmdPool,
  };

  const VkResult result = m_swapchain.init(swapChainInit);
  if (result != VK_SUCCESS)
  {
    vk_utils::reportSwapchainDiagnostics(instance, swapChainInit);
    nvvk::CheckError::getInstance().check(result, "m_swapchain.init", __FILE__, __LINE__);
  }

  // Initialize Swapchain Resources (triggers resize logic)
  NVVK_CHECK(m_swapchain.initResources(m_windowSize, m_vsyncWanted));

  // 5. Finalize High-Level Wrappers
  // Now that basic Vulkan plumbing is done, create the high-level context
  m_ctx = create_vk_context(m_vkContext, m_descriptorPool,
                            {m_viewportSize.width, m_viewportSize.height}, m_compiler);
  m_render = std::make_shared<VulkanSceneRenderer>(m_ctx);

  // 6. Create Frame Submission Sync Objects
  createFrameSubmission(m_swapchain.getMaxFramesInFlight());
}

//-----------------------------------------------------------------------
//
void VulkanBackend::waitForFrameCompletion() const
{
  VkDevice device = m_vkContext.getDevice();
  // Wait until GPU has finished processing the frame that was using these resources previously
  // (numFramesInFlight frames ago)
  const VkSemaphoreWaitInfo waitInfo = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .semaphoreCount = 1,
      .pSemaphores = &m_frameTimelineSemaphore,
      .pValues = &m_frameData[m_frameRingCurrent]->frameNumber,
  };
  vkWaitSemaphores(device, &waitInfo, std::numeric_limits<uint64_t>::max());
}

void VulkanBackend::newFrame()
{
  IRenderBackend::newFrame();
  ImGui_ImplVulkan_NewFrame();
}

bool VulkanBackend::beginFrame(FrameContext& frame)
{
  if (m_swapchain.needRebuilding())
  {
    NVVK_CHECK(m_swapchain.reinitResources(m_windowSize, m_vsyncWanted));
  }

  waitForFrameCompletion();  // Wait until GPU has finished processing

  // Acquire
  VkDevice device = m_vkContext.getDevice();
  VkResult res = m_swapchain.acquireNextImage(device);

  if (!(res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR))
  {
    return false;
  }  // Continue only if we got a valid image

  m_frameData[m_frameRingCurrent]->frameNumber += m_swapchain.getMaxFramesInFlight();

  // Prepare Command Buffer
  auto& ctx = m_frameData[m_frameRingCurrent];
  vkResetCommandPool(device, m_frameData[m_frameRingCurrent]->cmdPool, 0);

  VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(ctx->cmdBuffer, &beginInfo);

  // Reset the extra semaphores and command buffers
  m_waitSemaphores.clear();
  m_signalSemaphores.clear();
  m_commandBuffers.clear();

  return true;
}

void VulkanBackend::renderFrame(const std::vector<std::shared_ptr<core::IAppElement>>& elements,
                                FrameContext const& frame)
{

  std::vector<std::unique_ptr<VulkanRenderContext>> m_frameData{};
  auto& ctx = m_frameData[m_frameRingCurrent];
  VkCommandBuffer cmd = ctx->cmdBuffer;
  for (const std::shared_ptr<IAppElement>& e : elements)
  {
    e->onRender(static_cast<RenderContext*>(ctx.get()), frame);
  }

  // Start rendering to the swapchain
  beginDynamicRenderingToSwapchain(cmd);
  {
    nvvk::DebugUtil::ScopedCmdLabel scopedCmdLabel(cmd, "ImGui");
    // The ImGui draw commands are recorded to the command buffer, which includes the display of our
    // GBuffer image
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
  }
  endDynamicRenderingToSwapchain(cmd);

  // Prepare to submit the current frame for rendering
  // First add the swapchain semaphore to wait for the image to be available.
  m_waitSemaphores.push_back({
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = m_swapchain.getImageAvailableSemaphore(),
      .stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
  });
  m_signalSemaphores.push_back({
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = m_swapchain.getRenderFinishedSemaphore(),
      .stageMask =
          VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,  // Ensure everything is done before presenting
  });
}

void VulkanBackend::endFrame(FrameContext const& frameCtx)
{
  auto& frame = m_frameData[m_frameRingCurrent];
  vkEndCommandBuffer(frame->cmdBuffer);
  frame->cmdBuffer == VK_NULL_HANDLE;

  // Calculate timeline signal value
  m_frameData[m_frameRingCurrent]->frameNumber = frameCtx.frameNumber + m_frameData.size();

  // Add timeline semaphore to signal when GPU completes this frame
  // The color attachment output stage is used since that's when the frame is fully rendered
  m_signalSemaphores.push_back({
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = m_frameTimelineSemaphore,
      .value = frameCtx.frameNumber,
      .stageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,  // Wait that everything is completed
  });

  // Adding the command buffer of the frame to the list of command buffers to submit
  // Note: extra command buffers could have been added to the list from other parts of the
  // application (elements)
  m_commandBuffers.push_back(
      {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = frame->cmdBuffer});

  // Populate the submit info to synchronize rendering and send the command buffer
  const VkSubmitInfo2 submitInfo{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .waitSemaphoreInfoCount = uint32_t(m_waitSemaphores.size()),  //
      .pWaitSemaphoreInfos = m_waitSemaphores.data(),  // Wait for the image to be available
      .commandBufferInfoCount = uint32_t(m_commandBuffers.size()),      //
      .pCommandBufferInfos = m_commandBuffers.data(),                   // Command buffer to submit
      .signalSemaphoreInfoCount = uint32_t(m_signalSemaphores.size()),  //
      .pSignalSemaphoreInfos = m_signalSemaphores.data(),  // Signal when rendering is finished
  };

  // Submit the command buffer to the GPU and signal when it's done
  NVVK_CHECK(vkQueueSubmit2(m_vkContext.getQueueInfo(0).queue, 1, &submitInfo, nullptr));
}

void VulkanBackend::present()
{
  m_swapchain.presentFrame(m_vkContext.getQueueInfo(0).queue);
  m_frameRingCurrent = (m_frameRingCurrent + 1) % m_frameData.size();
}

void VulkanBackend::onResize(const WindowSize& size)
{
  IRenderBackend::onResize(size);
  // Recreate the G-Buffer to the size of the viewport
  NVVK_CHECK(vkQueueWaitIdle(m_vkContext.getQueueInfo(0).queue));
  // TODO Should resize VulkanSceneRenderer
  // {
  //   VkCommandBuffer cmd{};
  //   NVVK_CHECK(nvvk::beginSingleTimeCommands(cmd, m_device, m_transientCmdPool));
  //   // Call the implementation of the UI rendering
  //   for (std::shared_ptr<IAppElement>& e : m_elements)
  //   {
  //     e->onResize(m_viewportSize);
  //   }
  //   NVVK_CHECK(nvvk::endSingleTimeCommands(cmd, m_device, m_transientCmdPool,
  //   m_queues[0].queue));
  // }
}

uint32_t VulkanBackend::getFrameCycleSize() const
{
  return static_cast<uint32_t>(m_frameData.size());
}

void VulkanBackend::shutdown()
{
  VkDevice device = m_vkContext.getDevice();
  vkDeviceWaitIdle(device);

  for (auto& data : m_frameData)
  {
    vkDestroyCommandPool(device, data->cmdPool, nullptr);
  }
  vkDestroySemaphore(device, m_frameTimelineSemaphore, nullptr);
  m_swapchain.deinit();
  vkDestroySurfaceKHR(m_vkContext.getInstance(), m_surface, nullptr);
}

void VulkanBackend::createFrameSubmission(uint32_t numFrames)
{
  assert(numFrames >= 2);  // Must have at least 2 frames in flight
  VkDevice device = m_vkContext.getDevice();

  // Initialize timeline semaphore with (numFrames - 1) to allow concurrent frame submission. See
  // details in README.md
  const uint64_t initialValue = (static_cast<uint64_t>(numFrames) - 1);
  VkSemaphoreTypeCreateInfo timelineCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .pNext = nullptr,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue = initialValue,
  };

  // Create timeline semaphore for GPU-CPU synchronization
  // This ensures resources aren't overwritten while still in use by the GPU
  const VkSemaphoreCreateInfo semaphoreCreateInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                                                  .pNext = &timelineCreateInfo};
  NVVK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_frameTimelineSemaphore));
  NVVK_DBG_NAME(m_frameTimelineSemaphore);

  // Create command pools and buffers for each frame
  // Each frame gets its own command pool to allow parallel command recording while previous frames
  // may still be executing on the GPU
  const VkCommandPoolCreateInfo cmdPoolCreateInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = m_vkContext.getQueueInfo(0).familyIndex,
  };

  m_frameData.resize(numFrames);
  for (uint32_t i = 0; i < numFrames; i++)
  {
    m_frameData[i] = std::make_unique<VulkanRenderContext>();
    m_frameData[i]->frameNumber = i;  // Track frame index for synchronization

    // Separate pools allow independent reset/recording of commands while other frames are still
    // in-flight
    NVVK_CHECK(vkCreateCommandPool(device, &cmdPoolCreateInfo, nullptr, &m_frameData[i]->cmdPool));
    NVVK_DBG_NAME(m_frameData[i]->cmdPool);

    const VkCommandBufferAllocateInfo commandBufferAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_frameData[i]->cmdPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    NVVK_CHECK(
        vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &m_frameData[i]->cmdBuffer));
    NVVK_DBG_NAME(m_frameData[i]->cmdBuffer);
  }
}

void VulkanBackend::requestScreenshot(const std::filesystem::path& filename, int quality)
{
  m_screenShotRequested = true;
  m_screenShotFilename = filename;
  // Making sure the screenshot is taken after the swapchain loop (remove the menu after click)
  m_screenShotFrame = (m_frameRingCurrent - 1 + m_swapchain.getMaxFramesInFlight()) %
                      m_swapchain.getMaxFramesInFlight();
}

void VulkanBackend::setWindowSize(const WindowSize& windowSize)
{
  m_windowSize = {windowSize.width, windowSize.height};
}

//-----------------------------------------------------------------------
// We are using dynamic rendering, which is a more flexible way to render to the swapchain image.
//
void VulkanBackend::beginDynamicRenderingToSwapchain(VkCommandBuffer cmd) const
{
  // Image to render to
  const VkRenderingAttachmentInfo colorAttachment{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = m_swapchain.getImageView(),
      .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,    // Clear the image (see clearValue)
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,  // Store the image (keep the image)
      .clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}},
  };

  // Details of the dynamic rendering
  const VkRenderingInfo renderingInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {{0, 0}, m_windowSize},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &colorAttachment,
  };

  // Transition the swapchain image to the color attachment layout, needed when using dynamic
  // rendering
  nvvk::cmdImageMemoryBarrier(cmd, {m_swapchain.getImage(), VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});

  vkCmdBeginRendering(cmd, &renderingInfo);
}

//-----------------------------------------------------------------------
// End of dynamic rendering.
// The image is transitioned back to the present layout, and the rendering is ended.
//
void VulkanBackend::endDynamicRenderingToSwapchain(VkCommandBuffer cmd)
{
  vkCmdEndRendering(cmd);

  // Transition the swapchain image back to the present layout
  nvvk::cmdImageMemoryBarrier(cmd,
                              {m_swapchain.getImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR});
}

void VulkanBackend::freeResourcesQueue()
{
  for (auto& func : m_resourceFreeQueue[m_frameRingCurrent])
  {
    func();  // Free resources in queue
  }
  m_resourceFreeQueue[m_frameRingCurrent].clear();
}

}  // namespace core
