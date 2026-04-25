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
  linear float4 debugColor: COLOR0;
  nointerpolation uint instanceIndex;
};

// Output of the fragment shader
struct PSout
{
  float4 color : SV_Target;
};

// Define the Payload structure
#define TASK_WORKGROUP_SIZE 32
struct MeshTaskPayload {
    uint meshletIndices[TASK_WORKGROUP_SIZE];
    uint instanceIndices[TASK_WORKGROUP_SIZE]; 
};


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
        SceneInfo *si = pushConst.sceneInfoAddress;
        sceneInfo = *si;
        SceneResources *sceneResources = pushConst.resourcesAddress;
        instance  = sceneResources.instances[instanceIndex];
        meshIo    = sceneResources.meshes[instance.meshIndex];
    
}

// Fetch per-vertex attributes for the vertex stage.
// Both Vulkan and Metal use the same BufferView-based layout: attributes are
// read from the raw mesh data buffer (MeshPrimitive.buffer) using the offsets
// and strides described by the TriangleMesh BufferView structs.  This ensures
// both renderers share exactly the same geometry memory layout.
void fetchVertexAttributes(uint vertexIndex,
                           MeshPrimitive meshIo,
                           out float3 posMesh,
                           out float3 normal,
                           out float2 texCoord) {
    posMesh  = getAttribute<float3>(meshIo.buffer, meshIo.triMesh.positions, vertexIndex);
    normal   = getAttribute<float3>(meshIo.buffer, meshIo.triMesh.normals,   vertexIndex);
    texCoord = getAttribute<float2>(meshIo.buffer, meshIo.triMesh.texCoords, vertexIndex);
}

// Fetch SceneInfo / Instance / Material for the fragment stage.
void fetchFragmentResources(uint instanceIndex,
                            out SceneInfo sceneInfo,
                            out Instance  instance,
                            out Material  material) {
        SceneInfo *si = pushConst.sceneInfoAddress;
        sceneInfo = *si;
        SceneResources sceneResources = pushConst.resourcesAddress[0];
        instance  = sceneResources.instances[instanceIndex];
        material  = sceneResources.materials[instance.materialIndex];
}

// ---------------------------------------------------------------------------
// Fragment shading helper — keeps the fragment path pointer-free for Metal.
// ---------------------------------------------------------------------------
float3 computeFragmentColor(SceneInfo sceneInfo,
                            Instance  instance,
                            Material  material,
                            FragmentInput stage) {
    switch ((MaterialType)instance.hit_group) {
    case MaterialType::eDiffuse:
        return stage.debugColor.rgb;

    case MaterialType::eNormals:
        return (stage.worldNormal.rgb + 1.0f) * 0.5f;

    case MaterialType::eGltfPbr:
    default: {
        LightSample light = sampleLight(sceneInfo, stage.worldPos);

        float3 V = normalize(sceneInfo.cameraPosition - stage.worldPos);
        float3 N = normalize(stage.worldNormal);
        float3 L = normalize(light.direction);

        float3 albedo = material.baseColorFactor.xyz;
        if (material.baseColorTextureIndex > 0) {
            albedo *= textures[material.baseColorTextureIndex]
                          .Sample(stage.worldTexCoord)
                          .xyz;
        }

        float metallic  = material.metallicFactor;
        float roughness = material.roughnessFactor;

        float3 color = pbrMetallicRoughness(albedo, metallic, roughness, N, V, L);
        color *= light.color * light.intensity;

        float3 ambient = sceneInfo.backgroundColor;
        if (sceneInfo.useSky == 1) {
            float3 skyUpDir    = float3(0, 1, 0);
            float3 groundColor = sceneInfo.skySimpleParam.groundColor;
            float3 skyColor    = sceneInfo.skySimpleParam.skyColor;
            ambient = lerp(groundColor, skyColor, dot(N, skyUpDir) * 0.5 + 0.5);
        }

        return color + (ambient * albedo * 0.025);
    }
    }
}
