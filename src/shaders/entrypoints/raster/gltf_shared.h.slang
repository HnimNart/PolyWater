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
// These are declared unconditionally (Slang does not support __target_switch
// at module scope for variable declarations).  On Vulkan/SPIRV they are
// unreferenced and will not appear in the generated descriptor sets.
// MetalRasterPass::execute() binds them at the corresponding slots:
//   buffer(1) = SceneInfo, buffer(2) = Instances, buffer(3) = MeshPrimitives,
//   buffer(4) = Materials,  buffer(5) = per-draw vertex buffer.
// ---------------------------------------------------------------------------
ConstantBuffer<SceneInfo>       g_metalSceneInfoBuf;   // Metal buffer(1)
StructuredBuffer<Instance>      g_metalInstancesBuf;   // Metal buffer(2)
StructuredBuffer<MeshPrimitive> g_metalMeshesBuf;      // Metal buffer(3)
StructuredBuffer<Material>      g_metalMaterialsBuf;   // Metal buffer(4)
StructuredBuffer<MetalVertex>   g_metalVertexBuf;      // Metal buffer(5)

// ---------------------------------------------------------------------------
// Helper functions that wrap __target_switch so it does not appear directly
// inside shader entry points.  Slang 2026.5.x raises E36107 when a
// __target_switch statement is used directly inside a Metal entry point.
// ---------------------------------------------------------------------------

// Fetch SceneInfo / Instance / MeshPrimitive for the vertex stage.
void fetchVertexResources(int instanceIndex,
                          out SceneInfo     sceneInfo,
                          out Instance      instance,
                          out MeshPrimitive meshIo) {
    __target_switch {
    case metal: {
        sceneInfo = g_metalSceneInfoBuf;
        instance  = g_metalInstancesBuf[instanceIndex];
        meshIo    = g_metalMeshesBuf[instance.meshIndex];
    }
    default: {
        SceneInfo *si = pushConst.sceneInfoAddress;
        sceneInfo = *si;
        SceneResources *sceneResources = pushConst.resourcesAddress;
        instance  = sceneResources.instances[instanceIndex];
        meshIo    = sceneResources.meshes[instance.meshIndex];
    }
    }
}

// Fetch per-vertex attributes for the vertex stage.
void fetchVertexAttributes(uint vertexIndex,
                           MeshPrimitive meshIo,
                           out float3 posMesh,
                           out float3 normal,
                           out float2 texCoord) {
    __target_switch {
    case metal: {
        MetalVertex mv = g_metalVertexBuf[vertexIndex];
        posMesh  = mv.position;
        normal   = mv.normal;
        texCoord = mv.texcoord;
    }
    default: {
        posMesh  = getAttribute<float3>(meshIo.buffer, meshIo.triMesh.positions, vertexIndex);
        normal   = getAttribute<float3>(meshIo.buffer, meshIo.triMesh.normals,   vertexIndex);
        texCoord = getAttribute<float2>(meshIo.buffer, meshIo.triMesh.texCoords, vertexIndex);
    }
    }
}

// Fetch SceneInfo / Instance / Material for the fragment stage.
void fetchFragmentResources(uint instanceIndex,
                            out SceneInfo sceneInfo,
                            out Instance  instance,
                            out Material  material) {
    __target_switch {
    case metal: {
        sceneInfo = g_metalSceneInfoBuf;
        instance  = g_metalInstancesBuf[instanceIndex];
        material  = g_metalMaterialsBuf[instance.materialIndex];
    }
    default: {
        SceneInfo *si = pushConst.sceneInfoAddress;
        sceneInfo = *si;
        SceneResources sceneResources = pushConst.resourcesAddress[0];
        instance  = sceneResources.instances[instanceIndex];
        material  = sceneResources.materials[instance.materialIndex];
    }
    }
}
