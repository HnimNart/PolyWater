#pragma once

#include <cstdint>

//------------------------------------------------------------
// FrameContext
//------------------------------------------------------------
struct FrameContext
{
  uint64_t frameIndex = 0;   // Index within the frame ring
  bool vSyncWanted = 0;      //
  uint64_t frameCount = 0;   // Total frames in flight
  uint64_t frameNumber = 0;  // Monotonic frame counter
};

struct WindowSize
{
  uint32_t width;
  uint32_t height;
};