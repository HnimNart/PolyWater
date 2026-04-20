#pragma once

// Include the metal-cpp headers
#include <Metal/Metal.hpp>

// Forward declarations to keep the header clean
namespace app {
struct ApplicationCreateInfo;
}

// Assuming you will translate these custom classes to Metal as well
// class MetalAllocator;
// class MetalStagingUploader;

class MetalContextManager {
public:
  MetalContextManager() = default;
  ~MetalContextManager() { deinit(); }

  // Initialization and teardown
  bool init(const app::ApplicationCreateInfo &appInfo);
  void deinit();

  // Sub-system setup (Kept for architectural parity with Vulkan)
  void setupTransientCommandPool();
  void setupDescriptorPool();
  void setupAllocator();

  // Command Buffer Utilities
  MTL::CommandBuffer *startSingleTimeCmd();
  void endSingleTimeCmd(MTL::CommandBuffer *cmd);

  // Synchronization
  void waitForDeviceIdle();

  // Core Getters
  MTL::Device *getDevice() const { return m_device; }
  MTL::CommandQueue *getCommandQueue() const { return m_commandQueue; }

private:
  // Core Metal Objects
  MTL::Device *m_device{nullptr};
  MTL::CommandQueue *m_commandQueue{nullptr};

  // Configuration
  uint32_t m_maxTexturePool{1000};

  // Placeholders for your translated memory and upload managers
  // MetalAllocator m_allocator;
  // MetalStagingUploader m_stagingUploader;
};
