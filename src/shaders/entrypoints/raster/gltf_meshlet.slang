/*
 * Copyright (c) 2023-2025, NVIDIA CORPORATION.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gltf_shared.h.slang"
#include "include/bsdf/pbr.h.slang"
#include "include/light/lights.h.slang"
#include "shared/bindings.h"
#include "shared/slang_types.h"
#include "shared/structs.h"

/**********************************************************/
float3x3 safe_transpose(float3x3 m){

    // clang-format off
    __target_switch
     {case metal :
          // Manual transpose to bypass Metal Mesh Shader restrictions
          return float3x3(m[0][0], m[1][0], m[2][0], m[0][1], m[1][1], m[2][1],
                          m[0][2], m[1][2], m[2][2]);
          default:
  // This code is only validated/compiled for non-Metal targets
  return transpose(m);
  }

  // clang-format on
  }

  /**********************************************************/
  [shader("mesh")][numthreads(64, 1, 1)]  // 64 threads per workgroup (matches
                                          // max vertices per meshlet)
                  [outputtopology("triangle")] void
                  meshMain(in payload MeshTaskPayload taskPayload, uint threadId
                           : SV_GroupIndex, uint groupId
                           : SV_GroupID, out indices uint3 triangles[124],
                             out vertices FragmentInput outVertices[64])
  /**********************************************************/
  {
    SceneInfo* sceneInfo = pushConst.sceneInfoAddress.get();
    SceneResources* sceneResources = pushConst.resourcesAddress.get();

    // --- CRITICAL UPDATE ---
    // Read BOTH the instance index and the meshlet index from the payload!
    uint instanceIndex = taskPayload.instanceIndices[groupId];
    uint globalMeshletIndex = taskPayload.meshletIndices[groupId];

    // Now fetch the correct instance and mesh using the dynamically retrieved
    // index
    Instance instance = sceneResources->instances[instanceIndex];
    MeshPrimitive meshIo = sceneResources->meshes[instance.meshIndex];
    // Transpose of the inverse of the upper-left 3x3 of the transform matrix
    float3x3 normalMatrix =
        safe_transpose(inverse3x3(float3x3(instance.transform)));

    // Fetch the Meshlet header using the true global index
    GPUMeshlet meshlet =
        getMeshlet(meshIo.buffer, meshIo.meshlet.meshlets, globalMeshletIndex);

    // Set how many vertices and triangles this meshlet outputs
    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);

    // Parallel Vertex Fetching
    if (threadId < meshlet.vertexCount)
    {
      // Find the global vertex index in the shared mesh buffer
      uint globalVertexIndex =
          getMeshletVertex(meshIo.buffer, meshIo.meshlet.meshletVertices,
                           meshlet.vertexOffset + threadId);

      // Fetch the actual geometry data using the global index
      float3 posMesh = getAttribute<float3>(
          meshIo.buffer, meshIo.triMesh.positions, globalVertexIndex);
      float3 normal = getAttribute<float3>(
          meshIo.buffer, meshIo.triMesh.normals, globalVertexIndex);
      float2 texCoord = getAttribute<float2>(
          meshIo.buffer, meshIo.triMesh.texCoords, globalVertexIndex);

      // Transform to world space
      float4 pos = mul(float4(posMesh, 1.0), instance.transform);

      FragmentInput output;
      output.debugColor = colorFromIndex(globalMeshletIndex);
      output.instanceIndex = instanceIndex;
      output.sv_position = mul(pos, sceneInfo.viewProjMatrix);
      output.worldPos = pos.xyz;

      // Use the locally calculated normalMatrix
      output.worldNormal = normalize(mul(normal, normalMatrix));
      output.worldTexCoord = texCoord;

      // Write to the output array
      outVertices[threadId] = output;
    }

    // Parallel Triangle Fetching
    uint totalTriangles = meshlet.triangleCount;
    for (uint i = threadId; i < totalTriangles; i += 64)
    {
      // Fetch the 3 local 8-bit indices for this triangle
      uint indexOffset = meshlet.triangleOffset + (i * 3);

      uint8_t localIdx0 = getMeshletTriangle(
          meshIo.buffer, meshIo.meshlet.meshletTriangles, indexOffset + 0);
      uint8_t localIdx1 = getMeshletTriangle(
          meshIo.buffer, meshIo.meshlet.meshletTriangles, indexOffset + 1);
      uint8_t localIdx2 = getMeshletTriangle(
          meshIo.buffer, meshIo.meshlet.meshletTriangles, indexOffset + 2);

      // Write to the output primitive array
      triangles[i] = uint3(localIdx0, localIdx1, localIdx2);
    }
  }
