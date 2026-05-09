#ifdef __APPLE__

#import "metal_render_context.hpp"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>


namespace mtl
{

// Objective-C++ implementation struct (hidden from C++ consumers).
struct MetalRenderContextData {
  id<MTLCommandBuffer>          commandBuffer          = nil;
  MTLRenderPassDescriptor      *renderPassDescriptor   = nil;
  id<MTLRenderCommandEncoder>   renderCommandEncoder   = nil;
  id<CAMetalDrawable>           drawable               = nil;
};

/**********************************************************/
MetalRenderContext::MetalRenderContext()
    : m_data(std::make_unique<MetalRenderContextData>())
/**********************************************************/
{
}

/**********************************************************/
MetalRenderContext::~MetalRenderContext() = default;
/**********************************************************/

/**********************************************************/
void MetalRenderContext::submitBarriers(
    const std::vector<BarrierInfo> & /*barriers*/) const
/**********************************************************/
{
  // Metal uses automatic hazard tracking for resources in the same command
  // buffer. Explicit resource barriers are not required for basic rendering.
}

/**********************************************************/
void MetalRenderContext::setFrameData(void *commandBuffer,
                                      void *renderPassDescriptor,
                                      void *renderCommandEncoder,
                                      void *drawable)
/**********************************************************/
{
  m_data->commandBuffer =
      (__bridge id<MTLCommandBuffer>)commandBuffer;
  m_data->renderPassDescriptor =
      (__bridge MTLRenderPassDescriptor *)renderPassDescriptor;
  m_data->renderCommandEncoder =
      (__bridge id<MTLRenderCommandEncoder>)renderCommandEncoder;
  m_data->drawable =
      (__bridge id<CAMetalDrawable>)drawable;
}

/**********************************************************/
void *MetalRenderContext::getCommandBufferHandle() const
/**********************************************************/
{
  return (__bridge void *)m_data->commandBuffer;
}

/**********************************************************/
void *MetalRenderContext::getRenderPassDescriptorHandle() const
/**********************************************************/
{
  return (__bridge void *)m_data->renderPassDescriptor;
}

/**********************************************************/
void *MetalRenderContext::getRenderCommandEncoderHandle() const
/**********************************************************/
{
  return (__bridge void *)m_data->renderCommandEncoder;
}

/**********************************************************/
void *MetalRenderContext::getDrawableHandle() const
/**********************************************************/
{
  return (__bridge void *)m_data->drawable;
}


}  // namespace mtl

#endif // __APPLE__