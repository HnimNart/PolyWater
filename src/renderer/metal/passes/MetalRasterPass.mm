#ifdef __APPLE__

#import "MetalRasterPass.hpp"

#import <Metal/Metal.h>
#import <simd/simd.h>

#import "backend/metal/core/MetalContextManager.hpp"
#import "backend/metal/core/MetalRenderContext.hpp"
#import "renderer/metal/MetalDeviceAssets.hpp"
#import "scene/Scene.h"
#import "shaders/shared/structs.h"

#include <glm/gtx/string_cast.hpp>

// Slang-generated Metal shader sources (produced by the build system at
// ${CMAKE_BINARY_DIR}/_autogen/ which is on the compiler include path).
// Each header defines a const char[] variable named after the file with '.'
// replaced by '_', e.g. gltf_raster_slang / gltf_fragment_slang.
#include "gltf_fragment.slang.h"
#include "gltf_raster.slang.h"

// ---------------------------------------------------------------------------
// Internal pass state (ObjC objects kept behind an opaque pointer)
// ---------------------------------------------------------------------------
struct MetalRasterPassData {
  id<MTLDevice> device;
  id<MTLRenderPipelineState> pipelineState;
  id<MTLDepthStencilState> depthStencilState;
  bool ready = false;
};

// ---------------------------------------------------------------------------
// MetalRasterPass
// ---------------------------------------------------------------------------

/**********************************************************/
MetalRasterPass::MetalRasterPass(MetalContextManager *ctx,
                                 MetalDeviceAssets *assets)
    : m_ctx(ctx), m_assets(assets),
      m_data(std::make_unique<MetalRasterPassData>())
/**********************************************************/
{}

/**********************************************************/
MetalRasterPass::~MetalRasterPass()
/**********************************************************/
{
  deinit();
}

/**********************************************************/
void MetalRasterPass::init()
/**********************************************************/
{
  m_data->device = (__bridge id<MTLDevice>)m_ctx->getDeviceHandle();
  createPipeline();
}

/**********************************************************/
void MetalRasterPass::deinit()
/**********************************************************/
{
  m_data->pipelineState = nil;
  m_data->depthStencilState = nil;
  m_data->ready = false;
}

/**********************************************************/
void MetalRasterPass::setup(PassBuilder &builder)
/**********************************************************/
{
  // Rasterise directly into the swapchain colour + depth attachment.
  builder.write(RenderOutput::Swapchain, PipelineStage::Fragment,
                ResourceState::RenderTarget);
}

/**********************************************************/
void MetalRasterPass::createPipeline()
/**********************************************************/
{
  NSError *error = nil;
  id<MTLDevice> device = m_data->device;

  // --- Compile vertex library from Slang-generated Metal source ---
  NSString *vertSrc = [NSString stringWithUTF8String:gltf_raster_slang];
  id<MTLLibrary> vertLib =
      [device newLibraryWithSource:vertSrc options:nil error:&error];
  if (!vertLib) {
    NSLog(@"[MetalRasterPass] Vertex shader compile error: %@",
          error.localizedDescription);
    return;
  }
  id<MTLFunction> vertFn = [vertLib newFunctionWithName:@"vertexMain"];
  if (!vertFn) {
    NSLog(@"[MetalRasterPass] 'vertexMain' not found in vertex library");
    return;
  }

  // --- Compile fragment library from Slang-generated Metal source ---
  NSString *fragSrc = [NSString stringWithUTF8String:gltf_fragment_slang];
  id<MTLLibrary> fragLib =
      [device newLibraryWithSource:fragSrc options:nil error:&error];
  if (!fragLib) {
    NSLog(@"[MetalRasterPass] Fragment shader compile error: %@",
          error.localizedDescription);
    return;
  }
  id<MTLFunction> fragFn = [fragLib newFunctionWithName:@"fragmentMain"];
  if (!fragFn) {
    NSLog(@"[MetalRasterPass] 'fragmentMain' not found in fragment library");
    return;
  }

  // --- Render pipeline ---
  // The Slang vertex shader fetches all vertex data from GPU-virtual-address
  // pointers inside PushConstant; no stage_in vertex descriptor is needed.
  MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
  pd.vertexFunction = vertFn;
  pd.fragmentFunction = fragFn;
  pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
  pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

  m_data->pipelineState =
      [device newRenderPipelineStateWithDescriptor:pd error:&error];
  if (!m_data->pipelineState) {
    NSLog(@"[MetalRasterPass] Pipeline error: %@", error.localizedDescription);
    return;
  }

  // --- Depth-stencil state ---
  MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
  dd.depthCompareFunction = MTLCompareFunctionLess;
  dd.depthWriteEnabled = YES;
  m_data->depthStencilState = [device newDepthStencilStateWithDescriptor:dd];

  m_data->ready = true;
}

/**********************************************************/
void MetalRasterPass::execute(const IRenderContext &ctx)
/**********************************************************/
{
  if (!m_data->ready) {
    return;
  }

  const Scene *scene = ctx.sceneResources;
  if (!scene || scene->instances.empty()) {
    return;
  }

  const MetalRenderContext &metalCtx = MetalRenderContext::get(ctx);
  id<MTLRenderCommandEncoder> encoder =
      (__bridge id<MTLRenderCommandEncoder>)
          metalCtx.getRenderCommandEncoderHandle();
  if (!encoder) {
    return;
  }

  [encoder setRenderPipelineState:m_data->pipelineState];
  [encoder setDepthStencilState:m_data->depthStencilState];
  [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
  [encoder setCullMode:MTLCullModeBack];

  // Make every GPU-address-chained buffer resident before any draw call.
  // Without this Metal's hazard tracker doesn't know the shader will access
  // these buffers through pointer indirection, so the GPU reads zeros and
  // the viewport stays black.
  m_assets->useResources((__bridge void *)encoder);

  // One indexed draw call per instance.
  for (uint32_t i = 0; i < static_cast<uint32_t>(scene->instances.size());
       ++i) {
    if (i == 1) {
      continue;
    }
    const shaderio::Instance &inst = scene->instances[i];
    const uint32_t meshIdx = inst.meshIndex;

    void *ibPtr = m_assets->getIndexMetalBuffer(meshIdx);
    const uint32_t indexCount = m_assets->getIndexCount(meshIdx);
    if (!ibPtr || indexCount == 0) {
      continue;
    }
    id<MTLBuffer> ib = (__bridge id<MTLBuffer>)ibPtr;

    // Start from frame-global push constants and patch only draw-local state.
    shaderio::PushConstant pc = metalCtx.pushValues;
    pc.transform = inst.transform;
    pc.instanceIndex = static_cast<int>(i);
    // Slang compiles [[vk::push_constant]] to buffer(0) on both stages.
    auto size = sizeof(pc);
    [encoder setVertexBytes:&pc length:size atIndex:0];
    [encoder setFragmentBytes:&pc length:size atIndex:0];

    const MTLIndexType indexType = m_assets->is32BitIndex(meshIdx)
                                       ? MTLIndexTypeUInt32
                                       : MTLIndexTypeUInt16;

    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:indexCount
                         indexType:indexType
                       indexBuffer:ib
                 indexBufferOffset:0];
  }
}

#endif // __APPLE__
