#ifdef __APPLE__

#import "MetalRasterPass.hpp"

#import <Metal/Metal.h>
#import <simd/simd.h>

#import "backend/metal/core/MetalContextManager.hpp"
#import "backend/metal/core/MetalRenderContext.hpp"
#import "renderer/metal/MetalDeviceAssets.hpp"
#import "scene/Scene.h"
#import "shaders/shared/structs.h"

// ---------------------------------------------------------------------------
// Vertex layout matching MetalDeviceAssets::MetalVertex
// ---------------------------------------------------------------------------
struct MetalVertex {
  float position[3];
  float normal[3];
};

// Per-draw uniforms pushed via setVertexBytes / setFragmentBytes
struct PerDrawUniforms {
  simd_float4x4 modelMatrix;
  simd_float4x4 viewProjMatrix;
  simd_float4   baseColor;
};

struct MetalRasterPassData {
  id<MTLDevice>              device;
  id<MTLRenderPipelineState> pipelineState;
  id<MTLDepthStencilState>   depthStencilState;
  bool                       ready = false;
};

// ---------------------------------------------------------------------------
// Inline MSL source
// ---------------------------------------------------------------------------
static NSString *const kRasterShaderSrc = @R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 worldNormal;
    float3 worldPos;
};

struct PerDrawUniforms {
    float4x4 modelMatrix;
    float4x4 viewProjMatrix;
    float4   baseColor;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant PerDrawUniforms &u [[buffer(1)]])
{
    VertexOut out;
    float4 worldPos  = u.modelMatrix * float4(in.position, 1.0);
    out.position     = u.viewProjMatrix * worldPos;
    out.worldNormal  = normalize((u.modelMatrix * float4(in.normal, 0.0)).xyz);
    out.worldPos     = worldPos.xyz;
    return out;
}

fragment float4 fragment_main(VertexOut in       [[stage_in]],
                               constant PerDrawUniforms &u [[buffer(1)]])
{
    float3 N       = normalize(in.worldNormal);
    float3 L       = normalize(float3(1.0, 2.0, 1.0));
    float  diffuse = max(dot(N, L), 0.0);
    float  ambient = 0.15;
    float3 color   = u.baseColor.rgb * (ambient + diffuse * 0.85);
    return float4(color, 1.0);
}
)MSL";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static simd_float4x4 glmToSimd(const glm::mat4 &m)
{
  simd_float4x4 result;
  for (int col = 0; col < 4; ++col) {
    result.columns[col] = simd_make_float4(m[col][0], m[col][1],
                                           m[col][2], m[col][3]);
  }
  return result;
}

// ---------------------------------------------------------------------------
// MetalRasterPass
// ---------------------------------------------------------------------------

/**********************************************************/
MetalRasterPass::MetalRasterPass(MetalContextManager *ctx,
                                 MetalDeviceAssets   *assets)
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
  m_data->pipelineState    = nil;
  m_data->depthStencilState = nil;
  m_data->ready            = false;
}

/**********************************************************/
void MetalRasterPass::setup(PassBuilder &builder)
/**********************************************************/
{
  // Rasterise directly into the swapchain colour + depth attachment.
  // Metal uses automatic hazard tracking, so we just declare an intent.
  builder.write(RenderOutput::Swapchain, PipelineStage::Fragment,
                ResourceState::RenderTarget);
}

/**********************************************************/
void MetalRasterPass::createPipeline()
/**********************************************************/
{
  NSError *error = nil;
  id<MTLDevice> device = m_data->device;

  // --- Compile shaders ---
  id<MTLLibrary> lib = [device newLibraryWithSource:kRasterShaderSrc
                                            options:nil
                                              error:&error];
  if (!lib) {
    NSLog(@"[MetalRasterPass] Shader compile error: %@", error.localizedDescription);
    return;
  }

  id<MTLFunction> vertFn = [lib newFunctionWithName:@"vertex_main"];
  id<MTLFunction> fragFn = [lib newFunctionWithName:@"fragment_main"];

  // --- Vertex descriptor: matches MetalVertex layout ---
  MTLVertexDescriptor *vertDesc = [MTLVertexDescriptor new];
  // attribute 0: position (float3, offset 0)
  vertDesc.attributes[0].format      = MTLVertexFormatFloat3;
  vertDesc.attributes[0].offset      = 0;
  vertDesc.attributes[0].bufferIndex = 0;
  // attribute 1: normal (float3, offset 12)
  vertDesc.attributes[1].format      = MTLVertexFormatFloat3;
  vertDesc.attributes[1].offset      = 12;
  vertDesc.attributes[1].bufferIndex = 0;
  // buffer 0 layout: stride = sizeof(MetalVertex)
  vertDesc.layouts[0].stride         = sizeof(MetalVertex);
  vertDesc.layouts[0].stepFunction   = MTLVertexStepFunctionPerVertex;

  // --- Render pipeline ---
  MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
  pd.vertexFunction               = vertFn;
  pd.fragmentFunction             = fragFn;
  pd.vertexDescriptor             = vertDesc;
  pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
  pd.depthAttachmentPixelFormat   = MTLPixelFormatDepth32Float;

  m_data->pipelineState = [device newRenderPipelineStateWithDescriptor:pd
                                                                 error:&error];
  if (!m_data->pipelineState) {
    NSLog(@"[MetalRasterPass] Pipeline error: %@", error.localizedDescription);
    return;
  }

  // --- Depth-stencil state ---
  MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
  dd.depthCompareFunction  = MTLCompareFunctionLess;
  dd.depthWriteEnabled     = YES;
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
  id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)
      metalCtx.getRenderCommandEncoderHandle();
  if (!encoder) {
    return;
  }

  [encoder setRenderPipelineState:m_data->pipelineState];
  [encoder setDepthStencilState:m_data->depthStencilState];
  [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
  [encoder setCullMode:MTLCullModeBack];

  const simd_float4x4 viewProj =
      glmToSimd(scene->sceneInfo.viewProjMatrix);

  for (const shaderio::Instance &inst : scene->instances) {
    const uint32_t meshIdx = inst.meshIndex;

    void *vbPtr = m_assets->getVertexMetalBuffer(meshIdx);
    void *ibPtr = m_assets->getIndexMetalBuffer(meshIdx);
    const uint32_t indexCount = m_assets->getIndexCount(meshIdx);

    if (!vbPtr || !ibPtr || indexCount == 0) {
      continue;
    }

    id<MTLBuffer> vb = (__bridge id<MTLBuffer>)vbPtr;
    id<MTLBuffer> ib = (__bridge id<MTLBuffer>)ibPtr;

    // Build per-draw uniforms
    PerDrawUniforms uniforms;
    uniforms.modelMatrix  = glmToSimd(inst.transform);
    uniforms.viewProjMatrix = viewProj;

    // Material base colour
    glm::vec4 color(0.7f, 0.7f, 0.7f, 1.0f);
    if (inst.materialIndex < scene->materials.size()) {
      const shaderio::Material &mat = scene->materials[inst.materialIndex];
      color = mat.baseColorFactor;
    }
    uniforms.baseColor = simd_make_float4(color.r, color.g, color.b, color.a);

    [encoder setVertexBuffer:vb offset:0 atIndex:0];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];

    MTLIndexType indexType = m_assets->is32BitIndex(meshIdx)
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
