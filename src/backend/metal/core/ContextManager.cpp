#include "ContextManager.hpp"

#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <iostream>

// Helper macro for logging (equivalent to your LOGE/NVVK_DBG_NAME)
#define LOGE(...) fprintf(stderr, __VA_ARGS__)

/**********************************************************/
bool MetalContextManager::init(const app::ApplicationCreateInfo &appInfo)
/**********************************************************/
{
  // Metal heavily relies on autorelease pools for memory management of
  // short-lived objects during initialization.
  NS::AutoreleasePool *pPool = NS::AutoreleasePool::alloc()->init();

  // 1. Get the default Metal Device (Physical + Logical device equivalent)
  m_device = MTL::CreateSystemDefaultDevice();
  if (!m_device) {
    LOGE("Metal Initialization Failed: System does not support Metal.\n");
    pPool->release();
    return false;
  }

  // 2. Feature Checking
  // Metal feature checks are done via hardware families or direct capability
  // queries.
  bool supportsMeshShaders = m_device->supportsFamily(MTL::GPUFamilyApple7) ||
                             m_device->supportsFamily(MTL::GPUFamilyMac2);

  bool supportsRayTracing = m_device->supportsRaytracing();

  if (!supportsMeshShaders) {
    LOGE("Warning: Mesh Shaders are not supported on this Metal device.\n");
  }
  if (!supportsRayTracing) {
    LOGE("Warning: Ray Tracing is not supported on this Metal device.\n");
  }

  // Metal 3 dynamic state, push constants, and bindless are natively supported
  // on modern Apple Silicon without needing explicit extension enablement.

  // 3. Create the Command Queue (Equivalent to grabbing a Queue + Command Pool)
  m_commandQueue = m_device->newCommandQueue();
  if (!m_commandQueue) {
    LOGE("Metal Initialization Failed: Could not create Command Queue.\n");
    pPool->release();
    return false;
  }

  // Setup allocators and staging (Metal equivalents)
  setupTransientCommandPool();
  setupDescriptorPool();
  setupAllocator();

  pPool->release();
  return true;
}

/**********************************************************/
void MetalContextManager::setupTransientCommandPool()
/**********************************************************/
{
  // Metal does not require explicit command pools.
  // Command buffers are requested directly from the MTL::CommandQueue.
  // This function is kept for architectural parity but remains a no-op.
}

/**********************************************************/
void MetalContextManager::setupDescriptorPool()
/**********************************************************/
{
  // Metal uses Argument Buffers instead of Descriptor Sets.
  // Argument buffers are simply allocated from standard device memory
  // (MTL::Buffer). Therefore, no global descriptor pool needs to be created
  // upfront.
}

/**********************************************************/
void MetalContextManager::setupAllocator()
/**********************************************************/
{
  // Metal handles basic memory allocation directly via m_device->newBuffer().
  // If you are translating VMA, you would typically initialize a custom
  // MTL::Heap allocator here. For staging, shared memory is used on UMA
  // architectures.

  // Example wrapper initialization:
  // m_allocator.init(m_device);
  // m_stagingUploader.init(&m_allocator, true);
}

/**********************************************************/
MTL::CommandBuffer *MetalContextManager::startSingleTimeCmd()
/**********************************************************/
{
  // Fetch a command buffer from the queue. We retain it so it survives
  // outside of the current autorelease pool scope if necessary.
  MTL::CommandBuffer *cmd = m_commandQueue->commandBuffer();
  cmd->retain();
  return cmd;
}

/**********************************************************/
void MetalContextManager::endSingleTimeCmd(MTL::CommandBuffer *cmd)
/**********************************************************/
{
  if (!cmd)
    return;

  // Commit the command buffer to the GPU
  cmd->commit();

  // Wait for the GPU to finish executing this specific command buffer
  cmd->waitUntilCompleted();

  // Release the retained command buffer
  cmd->release();
}

/**********************************************************/
void MetalContextManager::waitForDeviceIdle()
/**********************************************************/
{
  // Metal does not have a global "deviceWaitIdle".
  // The closest equivalent is generating an empty command buffer, committing
  // it, and waiting for it to complete, ensuring all previously committed work
  // on this queue is done.

  NS::AutoreleasePool *pPool = NS::AutoreleasePool::alloc()->init();

  MTL::CommandBuffer *cmd = m_commandQueue->commandBuffer();
  cmd->commit();
  cmd->waitUntilCompleted();

  pPool->release();
}

/**********************************************************/
void MetalContextManager::deinit()
/**********************************************************/
{
  // Wait for ongoing operations to finish before destruction
  waitForDeviceIdle();

  // Deinitialize subsystems
  // m_stagingUploader.deinit();
  // m_allocator.deinit();

  // Release Metal objects
  if (m_commandQueue) {
    m_commandQueue->release();
    m_commandQueue = nullptr;
  }

  if (m_device) {
    m_device->release();
    m_device = nullptr;
  }
}
