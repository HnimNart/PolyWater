/*
 * Copyright (c) 2023-2025, NVIDIA CORPORATION.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "shared/slang_types.h"
#include "include/light/lights.h.slang"
#include "include/bsdf/pbr.h.slang"
#include "shared/structs.h"
#include "shared/bindings.h"

// Output of the mesh shader (Same as your old Vertex Shader output)
struct VSout
{
  float4 sv_position : SV_Position;
  float3 worldPos : POSITION;
  float3 worldNormal : NORMAL;
  float2 worldTexCoord : TEXCOORD0;
};

// Output of the fragment shader
struct PSout
{
  float4 color : SV_Target;
};

// --- Buffer Fetch Helpers ---

__generic<T : IFloat> T getAttribute(uint8_t* dataBufferAddress, BufferView bufferView, uint attributeIndex)
{
  if(bufferView.count > 0)
  {
    T* ptr = (T*)(dataBufferAddress + bufferView.offset + attributeIndex * bufferView.byteStride);
    return ptr[0];
  }
  return T(1);  // Error case
}

// Helper to fetch the GPUMeshlet struct from the raw buffer
GPUMeshlet getMeshlet(uint8_t* dataBufferAddress, BufferView bufferView, uint meshletIndex)
{
    GPUMeshlet* ptr = (GPUMeshlet*)(dataBufferAddress + bufferView.offset + meshletIndex * bufferView.byteStride);
    return ptr[0];
}

// Helper to fetch the 32-bit global vertex index
uint getMeshletVertex(uint8_t* dataBufferAddress, BufferView bufferView, uint elementIndex)
{
    uint* ptr = (uint*)(dataBufferAddress + bufferView.offset + elementIndex * bufferView.byteStride);
    return ptr[0];
}

// Helper to fetch the tightly packed 8-bit local triangle index
uint8_t getMeshletTriangle(uint8_t* dataBufferAddress, BufferView bufferView, uint elementIndex)
{
    uint8_t* ptr = (uint8_t*)(dataBufferAddress + bufferView.offset + elementIndex);
    return ptr[0];
}

// --- MESH SHADER ---

[shader("mesh")]
[numthreads(64, 1, 1)] // 64 threads per workgroup (matches max vertices per meshlet)
[outputtopology("triangle")]
void meshMain(
    uint threadId : SV_GroupIndex,
    uint groupId : SV_GroupID,
    out indices uint3 triangles[124],
    out vertices VSout outVertices[64]
)
{
  int instanceIndex = pushConst.instanceIndex;

  SceneInfo* sceneInfo = pushConst.sceneInfoAddress;
  SceneResources* sceneResources = pushConst.resourcesAddress;

  Instance instance = sceneResources.instances[instanceIndex];
  MeshPrimitive meshIo = sceneResources.meshes[instance.meshIndex];
  
  // 1. Fetch the Meshlet header for this workgroup
  // groupId is the meshlet index dispatched by vkCmdDrawMeshTasksEXT
  GPUMeshlet meshlet = getMeshlet(meshIo.buffer, meshIo.meshlet.meshlets, groupId);

  // 2. Set how many vertices and triangles this meshlet outputs
  SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);

  // 3. Parallel Vertex Fetching
  // Each thread in the group (0-63) processes one vertex if it's within the vertexCount
  if (threadId < meshlet.vertexCount)
  {
      // Find the global vertex index in the shared mesh buffer
      uint globalVertexIndex = getMeshletVertex(meshIo.buffer, meshIo.meshlet.meshletVertices, meshlet.vertexOffset + threadId);

      // Fetch the actual geometry data using the global index
      float3 posMesh = getAttribute<float3>(meshIo.buffer, meshIo.triMesh.positions, globalVertexIndex);
      float3 normal = getAttribute<float3>(meshIo.buffer, meshIo.triMesh.normals, globalVertexIndex);
      float2 texCoord = getAttribute<float2>(meshIo.buffer, meshIo.triMesh.texCoords, globalVertexIndex);

      // Transform to world space
      float4 pos = mul(float4(posMesh, 1.0), instance.transform);

      VSout output;
      output.sv_position = mul(pos, sceneInfo.viewProjMatrix);
      output.worldPos = pos.xyz;
      output.worldNormal = normalize(mul(normal, float3x3(pushConst.normalMatrix)));
      output.worldTexCoord = texCoord;

      // Write to the output array
      outVertices[threadId] = output;
  }

  // 4. Parallel Triangle Fetching
  // Each thread processes triangles. Since a meshlet can have up to 124 triangles, 
  // and we only have 64 threads, some threads need to do 2 loops.
  uint totalTriangles = meshlet.triangleCount;
  for (uint i = threadId; i < totalTriangles; i += 64)
  {
      // Fetch the 3 local 8-bit indices for this triangle
      uint indexOffset = meshlet.triangleOffset + (i * 3);
      
      uint8_t localIdx0 = getMeshletTriangle(meshIo.buffer, meshIo.meshlet.meshletTriangles, indexOffset + 0);
      uint8_t localIdx1 = getMeshletTriangle(meshIo.buffer, meshIo.meshlet.meshletTriangles, indexOffset + 1);
      uint8_t localIdx2 = getMeshletTriangle(meshIo.buffer, meshIo.meshlet.meshletTriangles, indexOffset + 2);

      // Write to the output primitive array
      triangles[i] = uint3(localIdx0, localIdx1, localIdx2);
  }
}

// --- FRAGMENT SHADER ---
// (Unchanged: The fragment shader doesn't care if a Vertex or Mesh shader fed it!)

[shader("pixel")] 
PSout fragmentMain(VSout stage)
{
  SceneInfo* sceneInfo = pushConst.sceneInfoAddress;
  SceneResources sceneResources = pushConst.resourcesAddress[0];
  Instance instance = sceneResources.instances[pushConst.instanceIndex];
  Material material = sceneResources.materials[instance.materialIndex];

  LightSample light = sampleLight(sceneInfo, stage.worldPos);

  float3 V = normalize(sceneInfo->cameraPosition - stage.worldPos);
  float3 N = normalize(stage.worldNormal);
  float3 L = normalize(light.direction);

  float3 albedo = material.baseColorFactor.xyz;
  if (material.baseColorTextureIndex > 0)
  {
    albedo *= textures[material.baseColorTextureIndex].Sample(stage.worldTexCoord).xyz;
  }

  float metallic = material.metallicFactor;
  float roughness = material.roughnessFactor;

  float3 color = pbrMetallicRoughness(albedo, metallic, roughness, N, V, L);
  color *= light.color * light.intensity;

  float3 ambient = sceneInfo->backgroundColor;
  if (sceneInfo->useSky == 1)
  {
    float3 skyUpDir = float3(0, 1, 0);
    float3 groundColor = sceneInfo->skySimpleParam.groundColor;
    float3 skyColor = sceneInfo->skySimpleParam.skyColor;
    ambient = lerp(groundColor, skyColor, dot(N, skyUpDir) * 0.5 + 0.5);
  }
  
  color += ambient * albedo * 0.025;

  PSout output;
  output.color = float4(clamp(color, float3(0.0), float3(1.0)), 1.0);

  return output;
}
