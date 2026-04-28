#ifdef __APPLE__

#import "MetalContextManager.hpp"

#import <Metal/Metal.h>

// Objective-C++ implementation struct (hidden from C++ consumers).
struct MetalContextManagerData {
  id<MTLDevice> device           = nil;
  id<MTLCommandQueue> commandQueue = nil;
};

/**********************************************************/
MetalContextManager::MetalContextManager()
    : m_data(std::make_unique<MetalContextManagerData>())
/**********************************************************/
{
}

/**********************************************************/
MetalContextManager::~MetalContextManager()
/**********************************************************/
{
  deinit();
}

/**********************************************************/
bool MetalContextManager::init(const app::ApplicationCreateInfo & /*appInfo*/)
/**********************************************************/
{
  m_data->device = MTLCreateSystemDefaultDevice();
  if (!m_data->device) {
    return false;
  }

  m_data->commandQueue = [m_data->device newCommandQueue];
  return m_data->commandQueue != nil;
}

/**********************************************************/
void MetalContextManager::deinit()
/**********************************************************/
{
  m_data->commandQueue = nil;
  m_data->device       = nil;
}

/**********************************************************/
void MetalContextManager::waitForIdle()
/**********************************************************/
{
  // Submit an empty command buffer and wait for it to complete.
  if (!m_data->commandQueue) {
    return;
  }
  id<MTLCommandBuffer> flushCmd = [m_data->commandQueue commandBuffer];
  if (flushCmd) {
    [flushCmd commit];
    [flushCmd waitUntilCompleted];
  }
}

/**********************************************************/
void *MetalContextManager::getDeviceHandle() const
/**********************************************************/
{
  return (__bridge void *)m_data->device;
}

/**********************************************************/
void *MetalContextManager::getCommandQueueHandle() const
/**********************************************************/
{
  return (__bridge void *)m_data->commandQueue;
}

#endif // __APPLE__
