/*
 * Copyright (c) 2023-2025, NVIDIA CORPORATION.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "shared/slang_types.h"
#include "include/light/lights.h.slang"
#include "include/bsdf/pbr.h.slang"
#include "shared/structs.h"
#include "shared/bindings.h"
#include "gltf_shared.h.slang"

[shader("mesh")]
[numthreads(64, 1, 1)] // 64 threads per workgroup (matches max vertices per meshlet)
[outputtopology("triangle")]
void meshMain(
    uint threadId : SV_GroupIndex,
    uint groupId : SV_GroupID,
    out indices uint3 triangles[124],
    out vertices FragmentInput outVertices[64]
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

      FragmentInput output;
      output.debugColor = colorFromIndex(groupId); // Example custom logic
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
