#pragma once

#ifdef __APPLE__

#include <memory>

#include "app/app_info.hpp"

// Forward declaration of the Objective-C++ implementation struct.
// Defined in MetalContextManager.mm.
struct MetalContextManagerData;

//------------------------------------------------------------
// MetalContextManager
//------------------------------------------------------------
// Manages the core Metal objects: device and command queue.
// Uses pImpl to keep Metal (Objective-C++) types out of the C++ header.
class MetalContextManager {
public:
  MetalContextManager();
  ~MetalContextManager();

  bool init(const app::ApplicationCreateInfo &appInfo);
  void deinit();
  void waitForIdle();

  // Returns opaque handles to the underlying Metal objects.
  // Callers in .mm files can cast these to id<MTLDevice> / id<MTLCommandQueue>.
  void *getDeviceHandle() const;
  void *getCommandQueueHandle() const;

private:
  std::unique_ptr<MetalContextManagerData> m_data;
};

#endif // __APPLE__
