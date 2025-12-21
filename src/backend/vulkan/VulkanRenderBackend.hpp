#pragma once

#include <memory>
#include <vector>

#include "VulkanContext.hpp"
#include "VulkanRenderContext.hpp"
#include "backend/IRenderBackend.hpp"
#include "backend/ISceneRenderer.hpp"
#include "backend/RenderContext.hpp"

class VulkanRenderBackend : public IRenderBackend
{
public:
  explicit VulkanRenderBackend(nvvk::Context& ctx)
  {
    m_context = std::make_unique<VulkanContext>(device, physicalDevice, viewport);
  }

  ~VulkanRenderBackend() override = default;

  VulkanRenderBackend(const VulkanRenderBackend&) = delete;
  VulkanRenderBackend& operator=(const VulkanRenderBackend&) = delete;
  VulkanRenderBackend(VulkanRenderBackend&&) = delete;
  VulkanRenderBackend& operator=(VulkanRenderBackend&&) = delete;

  // ------------------------------------------------------------------------
  // IRenderBackend overrides
  // ------------------------------------------------------------------------
  void initialize() override
  {
    for (auto& r : m_renderers)
    {
      r->init(*m_sceneResources);
    }
  }

  void shutdown() override
  {
    for (auto& r : m_renderers)
      r->clear();

    m_context.reset();
  }

  bool beginFrame(FrameContext& frame) override
  {
    // Acquire command buffer for this frame
    m_frameContext.commandBuffer = m_context->acquireCommandBuffer();
    m_frameContext.frameIndex = frame.index;
    return true;
  }

  RenderContext& getRenderContext() override { return m_frameContext; }

  void endFrame(FrameContext const& frame) override
  {
    // Submit command buffer, handle fences, etc.
    m_context->submitCommandBuffer(m_frameContext.commandBuffer);
  }

  void present() override { m_context->presentSwapchain(); }

  void setVsync(bool enabled) override { m_context->setVsync(enabled); }

  bool isVsync() const override { return m_context->isVsync(); }

  void resize(uint32_t width, uint32_t height) override { m_context->resize(width, height); }

  void requestScreenshot(const std::filesystem::path& filename, int quality = 100) override
  {
    m_screenshotFilename = filename;
    m_screenshotQuality = quality;
    m_screenshotRequested = true;
  }

  // ------------------------------------------------------------------------
  // Renderer management
  // ------------------------------------------------------------------------
  void addRenderer(std::shared_ptr<ISceneRenderer> renderer)
  {
    m_renderers.push_back(std::move(renderer));
  }

  VulkanContext& context() { return *m_context; }

private:
  std::unique_ptr<VulkanContext> m_context;
  VulkanRenderContext m_frameContext;

  std::vector<std::shared_ptr<ISceneRenderer>> m_renderers;

  std::shared_ptr<CpuSceneResources> m_sceneResources;

  // Screenshot state
  bool m_screenshotRequested{false};
  std::filesystem::path m_screenshotFilename;
  int m_screenshotQuality{100};
};
