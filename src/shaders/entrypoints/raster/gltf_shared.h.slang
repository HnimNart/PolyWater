#pragma once

#include "shared/slang_types.h"
#include "shared/bindings.h"
#include "shared/structs.h"

#include "include/bsdf/pbr.h.slang"
#include "include/geometry/vertex_fetch.h.slang"
#include "include/light/lights.h.slang"
#include "include/core/color.h.slang"

[[vk::binding(BindRaster::eHiZTexture, 1)]] Texture2D<float> hiZTexture;
[[vk::binding(BindRaster::eHiZSampler, 1)]] SamplerState hiZSampler;


// Output of the mesh shader 
struct FragmentInput
{
  float4 sv_position : SV_Position;
  float3 worldPos : POSITION;
  float3 worldNormal : NORMAL;
  float2 worldTexCoord : TEXCOORD0;
  nointerpolation float4 debugColor: COLOR0;
  nointerpolation uint instanceIndex;
};

// Output of the fragment shader
struct PSout
{
  float4 color : SV_Target;
};

// Define the Payload structure
#define TASK_WORKGROUP_SIZE 32


// Update the payload to hold BOTH the meshlet index and its instance index
struct MeshTaskPayload {
    uint meshletIndices[TASK_WORKGROUP_SIZE];
    uint instanceIndices[TASK_WORKGROUP_SIZE]; 
};

// ---------------------------------------------------------------------------
// Interleaved vertex layout built by MetalDeviceAssets::uploadSceneResoures().
// Used by the Metal rasterisation path to read per-vertex data from an
// explicitly-bound buffer, bypassing BDA pointer chains.
// ---------------------------------------------------------------------------
struct MetalVertex {
    float3 position;
    float3 normal;
    float2 texcoord;
};

// ---------------------------------------------------------------------------
// Metal-specific explicit buffer bindings (buffer slots 1-5).
// These are bound by MetalRasterPass::execute() and avoid the multi-level
// BDA pointer chain that goes through PushConstant.  On Vulkan/SPIRV only
// the default path (BDA via pushConst) is compiled in.
// ---------------------------------------------------------------------------
__target_switch {
case metal: {
    ConstantBuffer<SceneInfo>      g_metalSceneInfoBuf;   // Metal buffer(1)
    StructuredBuffer<Instance>     g_metalInstancesBuf;   // Metal buffer(2)
    StructuredBuffer<MeshPrimitive> g_metalMeshesBuf;     // Metal buffer(3)
    StructuredBuffer<Material>     g_metalMaterialsBuf;   // Metal buffer(4)
    StructuredBuffer<MetalVertex>  g_metalVertexBuf;      // Metal buffer(5)
}
}
