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
float3x3 safe_transpose(float3x3 m)
/**********************************************************/
{

    // clang-format off
    __target_switch
     {case metal :
          // Manual transpose to bypass Metal Mesh Shader restrictions
          return float3x3(m[0][0], m[1][0], m[2][0], m[0][1], m[1][1], m[2][1],
                          m[0][2], m[1][2], m[2][2]);
          default:
  return transpose(m);
  }

  // clang-format on
  }

  /**********************************************************/
  [shader("mesh")][numthreads(64, 1, 1)]
                  [outputtopology("triangle")] void
                  meshMain(in payload MeshTaskPayload taskPayload, uint threadId
                           : SV_GroupIndex, uint groupId
                           : SV_GroupID, out indices uint3 triangles[124],
                             out vertices FragmentInput outVertices[64])
  /**********************************************************/
  {
    SceneInfo* sceneInfo = pushConst.sceneInfoAddress.get();
    SceneResources* sceneResources = pushConst.resourcesAddress.get();

    uint instanceIndex = taskPayload.instanceIndices[groupId];
    uint globalMeshletIndex = taskPayload.meshletIndices[groupId];

    Instance instance = sceneResources->instances[instanceIndex];
    MeshPrimitive meshIo = sceneResources->meshes[instance.meshIndex];
    float3x3 normalMatrix =
        safe_transpose(inverse3x3(float3x3(instance.transform)));

    GPUMeshlet meshlet =
        getMeshlet(meshIo.buffer, meshIo.meshlet.meshlets, globalMeshletIndex);

    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);

    if (threadId < meshlet.vertexCount)
    {
      uint globalVertexIndex =
          getMeshletVertex(meshIo.buffer, meshIo.meshlet.meshletVertices,
                           meshlet.vertexOffset + threadId);

      float3 posMesh = getAttribute<float3>(
          meshIo.buffer, meshIo.triMesh.positions, globalVertexIndex);
      float3 normal = getAttribute<float3>(
          meshIo.buffer, meshIo.triMesh.normals, globalVertexIndex);
      float2 texCoord = getAttribute<float2>(
          meshIo.buffer, meshIo.triMesh.texCoords, globalVertexIndex);

      float4 pos = mul(float4(posMesh, 1.0), instance.transform);

      FragmentInput output;
      output.debugColor = colorFromIndex(globalMeshletIndex);
      output.instanceIndex = instanceIndex;
      output.sv_position = mul(pos, sceneInfo.viewProjMatrix);
      output.worldPos = pos.xyz;

      output.worldNormal = normalize(mul(normal, normalMatrix));
      output.worldTexCoord = texCoord;

      outVertices[threadId] = output;
    }

    uint totalTriangles = meshlet.triangleCount;
    for (uint i = threadId; i < totalTriangles; i += 64)
    {
      uint indexOffset = meshlet.triangleOffset + (i * 3);

      uint8_t localIdx0 = getMeshletTriangle(
          meshIo.buffer, meshIo.meshlet.meshletTriangles, indexOffset + 0);
      uint8_t localIdx1 = getMeshletTriangle(
          meshIo.buffer, meshIo.meshlet.meshletTriangles, indexOffset + 1);
      uint8_t localIdx2 = getMeshletTriangle(
          meshIo.buffer, meshIo.meshlet.meshletTriangles, indexOffset + 2);

      triangles[i] = uint3(localIdx0, localIdx1, localIdx2);
    }
  }
