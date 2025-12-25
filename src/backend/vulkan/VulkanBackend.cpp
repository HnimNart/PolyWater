#include "VulkanBackend.hpp"

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_vulkan.h>
#include <volk.h>
#include <vulkan/vulkan_core.h>

#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/swapchain.hpp>

#include "vk_utils.hpp"

namespace core
{

std::unique_ptr<VulkanBackend> VulkanBackend::create(const core::ApplicationCreateInfo appInfo)
{
  // Initialize the Vulkan context
  nvvk::ContextInitInfo vkSetup = vk_utils::setupVulkanContext(appInfo);
  nvvk::Context vkContext;
  if (vkContext.init(vkSetup) != VK_SUCCESS)
  {
    LOGE("Error in Vulkan context creation\n");
    assert(0);
  }
  return std::make_unique<VulkanBackend>(vkContext, nullptr);
}

VulkanBackend::VulkanBackend(nvvk::Context& vkContext, GLFWwindow* window) :
    m_windowHandle(window), m_vkContext(vkContext)
{
}

void VulkanBackend::init()
{
  VkDevice device = m_vkContext.getDevice();
  VkPhysicalDevice physicalDevice = m_vkContext.getPhysicalDevice();
  VkInstance instance = m_vkContext.getInstance();
  const nvvk::QueueInfo& graphics_queue = m_vkContext.getQueueInfo(0);

  // Create the window surface
  NVVK_CHECK(
      glfwCreateWindowSurface(m_vkContext.getInstance(), m_windowHandle, nullptr, &m_surface));

  // CreateTransientCommandPool
  const VkCommandPoolCreateInfo commandPoolCreateInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,  // Hint that commands will be short-lived
      .queueFamilyIndex = graphics_queue.familyIndex,
  };
  NVVK_CHECK(vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &m_transientCmdPool));
  NVVK_DBG_NAME(m_transientCmdPool);

  // createDescriptorPool()
  const std::array<VkDescriptorPoolSize, 1> poolSizes{
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_maxTexturePool},
  };

  const VkDescriptorPoolCreateInfo poolInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags =
          VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT |   //  allows descriptor sets to be
                                                              //  updated after they have been bound
                                                              //  to a command buffer
          VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,  // individual descriptor sets can be
                                                              // freed from the descriptor pool
      .maxSets = m_maxTexturePool,  // Allowing to create many sets (ImGui uses this for textures)
      .poolSizeCount = uint32_t(poolSizes.size()),
      .pPoolSizes = poolSizes.data(),
  };
  NVVK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool));
  NVVK_DBG_NAME(m_descriptorPool);

  // Create the swapchain
  nvvk::Swapchain::InitInfo swapChainInit{
      .physicalDevice = physicalDevice,
      .device = device,
      .queue = graphics_queue,
      .surface = m_surface,
      .cmdPool = m_transientCmdPool,
      // .preferredVsyncOffMode = info.preferredVsyncOffMode,
      // .preferredVsyncOnMode = info.preferredVsyncOnMode,
  };
  // We do some custom error-handling here to provide additional information
  // about the reason creating the swapchain failed.
  const VkResult result = m_swapchain.init(swapChainInit);
  if (VK_SUCCESS != result)
  {
    vk_utils::reportSwapchainDiagnostics(instance, swapChainInit);
    // So that this is treated the same way as other NVVK_CHECK errors:
    nvvk::CheckError::getInstance().check(result, "m_swapchain.init(swapChainInit)", __FILE__,
                                          __LINE__);
  }
  // Update the window size to the actual size of the surface
  NVVK_CHECK(m_swapchain.initResources(m_windowSize, m_vsyncWanted));

  // Create what is needed to submit the scene for each frame in-flight
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

bool VulkanBackend::beginFrame(FrameContext& frame)
{
  VkDevice device = m_vkContext.getDevice();

  waitForFrameCompletion();

  // Acquire
  VkResult res = m_swapchain.acquireNextImage(device);
  if (res == VK_ERROR_OUT_OF_DATE_KHR)
  {
    resize({0, 0});  // Trigger resize logic
    return false;
  }

  // Prepare Command Buffer
  auto& ctx = m_frameData[m_frameRingCurrent];
  vkResetCommandPool(device, m_frameData[m_frameRingCurrent]->cmdPool, 0);

  VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(ctx->cmdBuffer, &beginInfo);

  return true;
}

void VulkanBackend::renderFrame(FrameContext const& frame)
{
  // This is where internal backend rendering (like ImGui) would happen.
  // The Application loop calls onRender() on elements BEFORE calling this.
}

void VulkanBackend::endFrame(FrameContext const& frame)
{
  auto& ctx = m_frameData[m_frameRingCurrent];
  vkEndCommandBuffer(ctx->cmdBuffer);

  // Calculate timeline signal value
  m_frameData[m_frameRingCurrent]->frameNumber = frame.frameNumber + m_frameData.size();

  // Add timeline semaphore to signal when GPU completes this frame
  // The color attachment output stage is used since that's when the frame is fully rendered
  m_signalSemaphores.push_back({
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = m_frameTimelineSemaphore,
      .value = frame.frameNumber,
      .stageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,  // Wait that everything is completed
  });

  // Adding the command buffer of the frame to the list of command buffers to submit
  // Note: extra command buffers could have been added to the list from other parts of the
  // application (elements)
  m_commandBuffers.push_back(
      {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = ctx->cmdBuffer});

  VkSemaphore waitSem = m_swapchain.getImageAvailableSemaphore();
  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSemaphore signalSem = m_swapchain.getRenderFinishedSemaphore();

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

void VulkanBackend::resize(const WindowSize& size)
{

  // Check for DPI scaling and adjust the font size
  float xscale, yscale;
  glfwGetWindowContentScale(m_windowHandle, &xscale, &yscale);
  ImGui::GetIO().FontGlobalScale *= xscale / m_dpiScale;
  m_dpiScale = xscale;

  m_viewportSize = {size.width, size.height};
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

}  // namespace core
